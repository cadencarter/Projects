#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include "index_html.h"


const char *AP_SSID = "SSID";
const char *AP_PASS = "password"; // at least 8 characters

WebServer server(80);

// ---- Light attachment ----
// The lamp has one button that steps through its modes in this order, back
// to Off after the last one (the page uses the same order):
//   0 Off, 1 Spotlight, 2 Floodlight, 3 Ultra Bright, 4 Red Light, 5 Flashing Red
// The hub can't read the lamp's mode, so it counts button presses. The count
// starts at 0 (Off) each time the light attachment is plugged in.
//
// If the button hasn't been pressed for 6 seconds (half second buffer), the lamp's next press
// turns it Off instead of moving to the next mode. Presses after that step
// through the modes from Off again.
#define LIGHT_MODES 6
const unsigned long LIGHT_TIMEOUT_MS = 6UL * 1000UL; // 6 seconds
int lightMode = 0;
unsigned long lastLightPress = 0; // millis() of the last press (or plug-in)

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
#define USB_DP_PIN 20 // D+ (also presses the lamp's button, see pressButton)

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

#define PRESS_MS 150 // try 100-300 if the lamp doesn't respond

// Presses the lamp's button by driving D+ HIGH: current flows through the
// light's 330 ohm resistor into its optocoupler. Never drive D- HIGH: it goes
// straight to the optocoupler's LED with no resistor.
void pressButton()
{
  pinMode(USB_DP_PIN, OUTPUT);
  digitalWrite(USB_DP_PIN, HIGH);
  delay(PRESS_MS);
  digitalWrite(USB_DP_PIN, LOW);
  releaseIdPins(); // back to detection
}

void handleToggle()
{
  if (strcmp(attachment, "light") != 0)
  {
    server.send(409, "text/plain", "No light attached");
    return;
  }

  pressButton();

  // 6 seconds or more since the last press: this press turns the lamp Off.
  if (millis() - lastLightPress >= LIGHT_TIMEOUT_MS)
    lightMode = 0;
  else
    lightMode = (lightMode + 1) % LIGHT_MODES;
  lastLightPress = millis();

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
  server.on("/status", handleStatus);
  server.begin();

  udp.begin(ATTACHMENT_UDP_PORT);
}

void loop()
{
  server.handleClient();
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
