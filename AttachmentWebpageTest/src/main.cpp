#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include "index_html.h"

const char *AP_SSID = "SSID";
const char *AP_PASS = "password"; // at least 8 characters

// ---- Web server ----
// The ESP32 WebServer library serves one connection at a time. When a browser
// opens a connection and doesn't send a request on it straight away (Chrome
// often opens spare connections in advance), the library keeps waiting on it
// for up to 5 s, and every other request - including button presses - waits
// behind it. This version gives up on such connections after
// IDLE_CONNECTION_WAIT_MS instead. It copies WebServer::handleClient() from
// ESP32 Arduino core 2.x (what PlatformIO's espressif32 platform uses), so on
// core 3.x it falls back to the normal WebServer.
const unsigned long IDLE_CONNECTION_WAIT_MS = 300;

#if ESP_ARDUINO_VERSION_MAJOR < 3
class FastWebServer : public WebServer
{
public:
  using WebServer::WebServer;

  void handleClient() override
  {
    if (_currentStatus == HC_NONE)
    {
      _currentClient = _server.available();
      if (!_currentClient)
      {
        if (_nullDelay)
          delay(1);
        return;
      }
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
    }

    bool keepCurrentClient = false;
    if (_currentClient.connected() && _currentStatus == HC_WAIT_READ)
    {
      if (_currentClient.available())
      {
        if (_parseRequest(_currentClient))
        {
          _currentClient.setTimeout(HTTP_MAX_SEND_WAIT / 1000);
          _contentLength = CONTENT_LENGTH_NOT_SET;
          _handleRequest();
        }
      }
      else if (millis() - _statusChange <= IDLE_CONNECTION_WAIT_MS)
      {
        keepCurrentClient = true; // give the browser a moment to send the request
        yield();
      }
    }

    if (!keepCurrentClient)
    {
      _currentClient = WiFiClient();
      _currentStatus = HC_NONE;
      _currentUpload.reset();
      _currentRaw.reset();
    }
  }
};
FastWebServer server(80);
#else
WebServer server(80);
#endif

// ---- Light attachment ----
// The lamp has one button that steps through its modes in this order, back
// to Off after the last one (the page uses the same order):
//   0 Off, 1 Spotlight, 2 Floodlight, 3 Ultra Bright, 4 Red Light, 5 Flashing Red
// The hub can't read the lamp's mode, so it counts button presses. The count
// starts at 0 (Off) each time the light attachment is plugged in.
//
// If the lamp is on and the button hasn't been pressed for 6 seconds (half
// second buffer), the next press turns it Off instead of moving to the next
// mode. When the lamp is Off, a press always turns it on to Spotlight,
// however long it has been. Presses after that step through the modes again.
#define LIGHT_MODES 6
const unsigned long LIGHT_TIMEOUT_MS = 6UL * 1000UL; // 6 seconds
int lightMode = 0;
unsigned long lastLightPress = 0; // millis() of the last press (or plug-in)

// Button presses run in loop() instead of inside the web request, so the page
// gets its answer straight away and the server isn't held up. Presses asked
// for while one is still running wait their turn.
//
// The lamp ignores the hub's very first press after the light is plugged in
// (a longer press didn't help). So that first click on the page sends two
// presses: a wake-up press, then after WAKE_GAP_MS the press that turns it
// on. The gap is long enough that the lamp doesn't treat them as a
// double-click. Later clicks send one press each.
#define PRESS_MS 300       // how long D+ is held HIGH for each press
#define PRESS_GAP_MS 150   // pause between queued presses
#define WAKE_GAP_MS 1000   // pause between the wake-up press and the real one
#define MAX_QUEUED_PRESSES 8
struct QueuedPress
{
  unsigned int gapBeforeMs; // wait this long after the previous press
  bool wake;                // wake-up press (for the serial log)
};
QueuedPress pressQueue[MAX_QUEUED_PRESSES];
bool firstPressSincePlugIn = true; // set again each time the light is plugged in
int pendingPresses = 0;
bool pressing = false;
unsigned long pressStateAt = 0; // when the current press (or gap) started

void queuePress(unsigned int gapBeforeMs, bool wake)
{
  pressQueue[pendingPresses++] = {gapBeforeMs, wake};
}

// ---- Attachment detection (USB-C) ----
// Attachments plug into the DevKit's native "USB" port (not "COM"). That port
// only reaches two ESP32 pins, used here as plain digital pins, not for USB:
//   D- = GPIO19, D+ = GPIO20
// Both pins use the ESP32's internal pull-up resistors, so an unconnected line
// reads HIGH and a line with a resistor to GND reads LOW:
//   D-    D+
//   LOW   LOW   -> light (D- and D+ both reach GND through its resistors)
//   HIGH  LOW   -> GPS + environment (resistor from D+ to GND, D- left open)
//   HIGH  HIGH  -> nothing plugged in
//   LOW   HIGH  -> not a known attachment (check wiring)
// The pins can't be used to measure voltage here: they are on ADC2, which
// doesn't work while WiFi is on. Plain HIGH/LOW reads work fine.
// The internal pull-ups are weak (~45 kOhm), so detection never turns on the
// light's optocoupler.
//
// Serial must stay on the "COM" port (no ARDUINO_USB_CDC_ON_BOOT), because
// the native USB port's pins are used for the attachments.
#define USB_DM_PIN 19 // D-
#define USB_DP_PIN 20 // D+ (also presses the lamp's button, see updatePress)

// A new pattern must be read this many times in a row (250 ms apart)
// before the attachment changes, so plugging in doesn't flicker.
const int DETECT_STABLE_READS = 3;

bool dmHigh = true, dpHigh = true; // last pin readings
const char *attachment = "none";
String detectReason = "Starting up";

// ---- GPS + environment attachment ----
// Its own ESP32 joins this access point and sends one JSON reading every 3 s
// by UDP.
const uint16_t ATTACHMENT_UDP_PORT = 4210; // must match RECEIVER_PORT in Attachment_GPSandEnviro
WiFiUDP udp;
String latestReading = "";
unsigned long latestReadingAt = 0;
uint32_t packetsReceived = 0;
uint32_t packetsRejected = 0;
IPAddress lastSender;
// ------------------------------

// Puts D- and D+ back into detection mode (inputs with pull-ups).
void releaseIdPins()
{
  pinMode(USB_DM_PIN, INPUT_PULLUP);
  pinMode(USB_DP_PIN, INPUT_PULLUP);
}

void updateAttachment()
{
  // D+ is driven HIGH during a press, so the pins can't be read until all
  // queued presses are done.
  if (pressing || pendingPresses > 0)
    return;

  dmHigh = digitalRead(USB_DM_PIN) == HIGH;
  dpHigh = digitalRead(USB_DP_PIN) == HIGH;

  const char *detected;
  if (!dmHigh && !dpHigh)
  {
    detected = "light";
    detectReason = "D- and D+ are both pulled to GND: light attachment";
  }
  else if (dmHigh && !dpHigh)
  {
    detected = "gps_env";
    detectReason = "Only D+ is pulled to GND: GPS + environment attachment";
  }
  else if (dmHigh && dpHigh)
  {
    detected = "none";
    detectReason = "D- and D+ are both HIGH: nothing plugged in, or the cable doesn't carry D-/D+ "
                   "(charge-only cables don't)";
  }
  else
  {
    detected = "none";
    detectReason = "Only D- is pulled to GND: not a known attachment, check the wiring";
  }

  // Only switch once the same result has been read a few times in a row.
  static const char *candidate = "none";
  static int sameCount = 0;
  if (strcmp(detected, candidate) == 0)
    sameCount++;
  else
  {
    candidate = detected;
    sameCount = 1;
  }
  if (sameCount < DETECT_STABLE_READS || strcmp(detected, attachment) == 0)
    return;

  Serial.printf("Attachment changed: %s -> %s\n", attachment, detected);
  attachment = detected;

  // A newly plugged-in lamp starts Off.
  if (strcmp(attachment, "light") == 0)
  {
    lightMode = 0;
    lastLightPress = millis();
    firstPressSincePlugIn = true;
  }
}

void receiveReadings()
{
  int len = udp.parsePacket();
  if (len <= 0)
    return;

  lastSender = udp.remoteIP();
  char buf[700];
  len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0)
    return;
  buf[len] = '\0';

  // Only keep packets that look like a JSON object.
  if (buf[0] == '{' && buf[len - 1] == '}')
  {
    latestReading = buf;
    latestReadingAt = millis();
    packetsReceived++;
  }
  else
  {
    packetsRejected++;
  }
}

// Detection details for the web page's Troubleshooting section.
String debugJson()
{
  String d = "{\"dm_pin\":" + String(USB_DM_PIN);
  d += ",\"dp_pin\":" + String(USB_DP_PIN);
  d += ",\"dm\":\"" + String(dmHigh ? "HIGH" : "LOW") + "\"";
  d += ",\"dp\":\"" + String(dpHigh ? "HIGH" : "LOW") + "\"";
  d += ",\"reason\":\"" + detectReason + "\"";
  d += ",\"wifi_clients\":" + String(WiFi.softAPgetStationNum());
  d += ",\"packets\":" + String(packetsReceived);
  d += ",\"packets_rejected\":" + String(packetsRejected);
  d += ",\"last_packet_ms\":" + (packetsReceived ? String(millis() - latestReadingAt) : String("null"));
  d += ",\"last_sender\":";
  if (packetsReceived)
    d += "\"" + lastSender.toString() + "\"";
  else
    d += "null";
  d += "}";
  return d;
}

void printDiagnostics()
{
  Serial.println("---- Attachment check ----");
  Serial.printf("D- (GPIO%d): %s   D+ (GPIO%d): %s\n", USB_DM_PIN, dmHigh ? "HIGH" : "LOW",
                USB_DP_PIN, dpHigh ? "HIGH" : "LOW");
  Serial.printf("Result: %s - %s\n", attachment, detectReason.c_str());
  Serial.printf("WiFi: %d device(s) connected, %lu packet(s) received", WiFi.softAPgetStationNum(),
                (unsigned long)packetsReceived);
  if (packetsReceived)
    Serial.printf(", last from %s %lu s ago", lastSender.toString().c_str(), (millis() - latestReadingAt) / 1000);
  if (packetsRejected)
    Serial.printf(", %lu rejected", (unsigned long)packetsRejected);
  Serial.println();
}

// What the web page asks for every 2 s: which attachment is connected and,
// for the GPS + environment attachment, its latest reading.
void handleStatus()
{
  String out = "{\"attachment\":\"";
  out += attachment;
  out += "\",\"debug\":";
  out += debugJson();
  if (strcmp(attachment, "gps_env") == 0 && latestReading.length())
  {
    out += ",\"age_ms\":";
    out += String(millis() - latestReadingAt);
    out += ",\"data\":";
    out += latestReading;
  }
  else
  {
    out += ",\"age_ms\":null,\"data\":null";
  }
  out += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void handleRoot() { server.send_P(200, "text/html", PAGE); }
void handleState() { server.send(200, "text/plain", String(lightMode)); }

// The hub can't see the lamp, so if a press is missed the page gets out of
// step. /sync?mode=N tells the hub which mode the lamp is really in, without
// pressing the button.
void handleSync()
{
  int mode = server.hasArg("mode") ? server.arg("mode").toInt() : -1;
  if (!server.hasArg("mode") || mode < 0 || mode >= LIGHT_MODES)
  {
    server.send(400, "text/plain", "mode must be 0-5");
    return;
  }
  Serial.printf("Light mode corrected from the page: %d -> %d\n", lightMode, mode);
  lightMode = mode;
  server.send(200, "text/plain", String(lightMode));
}

// Presses the lamp's button by driving D+ HIGH: current flows through the
// light's 330 ohm resistor into its optocoupler. Never drive D- HIGH: it goes
// straight to the optocoupler's LED with no resistor.
// Called from loop(): starts the next queued press, and ends it after its
// duration without blocking anything else.
void updatePress()
{
  unsigned long now = millis();
  if (pressing)
  {
    if (now - pressStateAt >= PRESS_MS)
    {
      digitalWrite(USB_DP_PIN, LOW);
      releaseIdPins(); // back to detection
      pressing = false;
      pressStateAt = now; // start of the gap before the next press
    }
  }
  else if (pendingPresses > 0 && now - pressStateAt >= pressQueue[0].gapBeforeMs)
  {
    bool wake = pressQueue[0].wake;
    for (int i = 1; i < pendingPresses; i++)
      pressQueue[i - 1] = pressQueue[i];
    pendingPresses--;
    lastLightPress = now; // the lamp's 6-second timer starts from its real last press
    Serial.printf("Pressing the lamp's button%s (%d ms)\n", wake ? " (wake-up)" : "", PRESS_MS);
    pinMode(USB_DP_PIN, OUTPUT);
    digitalWrite(USB_DP_PIN, HIGH);
    pressing = true;
    pressStateAt = now;
  }
}

void handleToggle()
{
  if (strcmp(attachment, "light") != 0)
  {
    server.send(409, "text/plain", "No light attached");
    return;
  }

  if (pendingPresses > MAX_QUEUED_PRESSES - 2)
  {
    server.send(503, "text/plain", "Too many presses waiting");
    return;
  }

  // Idle: no press waiting or running, and 6 seconds or more since the lamp's
  // last press.
  bool idle = pendingPresses == 0 && !pressing && millis() - lastLightPress >= LIGHT_TIMEOUT_MS;

  // Queue the press(es); updatePress() starts them within a few milliseconds.
  if (firstPressSincePlugIn && lightMode == 0)
  {
    queuePress(0, true);            // wake-up press, ignored by the lamp
    queuePress(WAKE_GAP_MS, false); // turns it on to Spotlight
    firstPressSincePlugIn = false;
  }
  else
  {
    queuePress(PRESS_GAP_MS, false);
  }

  // Lamp on and 6 seconds or more since the last press: this press turns it
  // Off. From Off, a click always goes to Spotlight (handled below).
  if (lightMode != 0 && idle)
    lightMode = 0;
  else
    lightMode = (lightMode + 1) % LIGHT_MODES;
  lastLightPress = millis(); // updated again when each press really happens
  Serial.printf("Button press requested: page now shows mode %d\n", lightMode);

  server.send(200, "text/plain", String(lightMode));
}

void setup()
{
  Serial.begin(115200);

  // Configuring GPIO19/20 as normal pins disconnects the chip's USB hardware
  // from them (done by the ESP-IDF GPIO driver).
  releaseIdPins();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Connect to Wi-Fi '");
  Serial.print(AP_SSID);
  Serial.println("', then open:");
  Serial.println(WiFi.softAPIP()); // 192.168.4.1 by default

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/state", handleState);
  server.on("/sync", handleSync);
  server.on("/status", handleStatus);
  server.begin();

  udp.begin(ATTACHMENT_UDP_PORT);
}

void loop()
{
  server.handleClient();
  updatePress();
  receiveReadings();

  static unsigned long lastCheck = 0, lastPrint = 0;
  if (millis() - lastCheck >= 250)
  {
    lastCheck = millis();
    updateAttachment();
  }
  if (millis() - lastPrint >= 2000)
  {
    lastPrint = millis();
    printDiagnostics();
  }
}
