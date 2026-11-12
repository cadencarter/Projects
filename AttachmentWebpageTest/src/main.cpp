#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include "index_html.h"

#define togglePin 4
#define high 1
#define low 0

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

// ---- Attachment detection ----
// Both attachments plug into the USB-C port and are identified by an ID
// resistor from a USB-C pin to GND. The hub has a reference resistor from
// that pin to 3.3 V, so the voltage on ID_PIN tells us the resistance:
//   3.3V -- R_REF -- ID_PIN -- R_ID (in attachment) -- GND
// R_REF must be set to that resistor's value; with 0 nothing can be
// detected. The light gets its mode changes over USB-C; the GPS +
// environment attachment sends its readings over WiFi.
//
// Troubleshooting: every 2 s the serial monitor prints what the hub
// measures and why it picked (or didn't pick) an attachment. The same
// information is on the web page under "Troubleshooting".
#define ID_PIN 5                // ADC pin wired to the USB-C ID line
const float R_REF = 0.0;        // reference resistor in the hub (ohms)
const float R_LIGHT = 440.0;    // ID resistor in the light attachment
const float R_GPS_ENV = 550.0;  // ID resistor in the GPS + environment attachment
const float R_TOLERANCE = 0.20; // accept +/- 20%

float idResistance = 0;  // last measured ID resistance in ohms (0 = none)
uint32_t idMillivolts = 0;     // average voltage on ID_PIN
uint32_t idMinMv = 0, idMaxMv = 0; // spread of the samples (noise check)
const char *attachment = "none";
String detectReason = "Starting up";

// ---- GPS + environment attachment ----
// It joins this access point and sends one JSON reading every 3 s by UDP.
const uint16_t ATTACHMENT_UDP_PORT = 4210; // must match RECEIVER_PORT in Attachment_GPSandEnviro
WiFiUDP udp;
String latestReading = "";
unsigned long latestReadingAt = 0;
uint32_t packetsReceived = 0;
uint32_t packetsRejected = 0;
IPAddress lastSender;
// ------------------------------

bool near(float value, float target) { return fabs(value - target) <= target * R_TOLERANCE; }

void updateAttachment()
{
  // Average a few readings to reduce noise, and keep the spread.
  uint32_t sum = 0;
  idMinMv = UINT32_MAX;
  idMaxMv = 0;
  for (int i = 0; i < 8; i++)
  {
    uint32_t v = analogReadMilliVolts(ID_PIN);
    sum += v;
    idMinMv = min(idMinMv, v);
    idMaxMv = max(idMaxMv, v);
  }
  idMillivolts = sum / 8;

  const char *previous = attachment;
  attachment = "none";
  idResistance = 0;

  if (R_REF <= 0)
  {
    detectReason = "R_REF is 0 in main.cpp: set it to the hub's reference resistor (ohms) so the resistance can be worked out";
  }
  else if (idMillivolts > 3200)
  {
    detectReason = String("ID pin is at 3.3 V: nothing plugged in, or the USB-C ID line isn't reaching GPIO") + ID_PIN;
  }
  else if (idMillivolts < 30)
  {
    detectReason = "ID pin is at 0 V: shorted to GND, or the pull-up resistor (R_REF) to 3.3 V isn't fitted";
  }
  else
  {
    idResistance = R_REF * idMillivolts / (3300.0 - idMillivolts);
    if (near(idResistance, R_LIGHT))
    {
      attachment = "light";
      detectReason = "Matches the light's ID resistor";
    }
    else if (near(idResistance, R_GPS_ENV))
    {
      attachment = "gps_env";
      detectReason = "Matches the GPS + environment ID resistor";
    }
    else
    {
      detectReason = String("Measured ") + String(idResistance, 0) + " ohm, which doesn't match any attachment";
    }
  }

  if (idMaxMv - idMinMv > 200)
    detectReason += " (reading is unstable: the pin may be floating or loosely connected)";

  // A newly plugged-in lamp starts Off.
  if (attachment != previous && strcmp(attachment, "light") == 0)
  {
    lightMode = 0;
    lastLightPress = millis();
  }
  if (attachment != previous)
    Serial.printf("Attachment changed: %s -> %s\n", previous, attachment);
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
  String d = "{\"id_pin\":" + String(ID_PIN);
  d += ",\"id_mv\":" + String(idMillivolts);
  d += ",\"id_min_mv\":" + String(idMinMv);
  d += ",\"id_max_mv\":" + String(idMaxMv);
  d += ",\"r_ref\":" + String(R_REF, 0);
  d += ",\"r_light\":" + String(R_LIGHT, 0);
  d += ",\"r_gps_env\":" + String(R_GPS_ENV, 0);
  d += ",\"tolerance_pct\":" + String(R_TOLERANCE * 100, 0);
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
  Serial.printf("ID pin GPIO%d: %lu mV (min %lu, max %lu)\n", ID_PIN, (unsigned long)idMillivolts,
                (unsigned long)idMinMv, (unsigned long)idMaxMv);
  if (idResistance > 0)
    Serial.printf("Measured resistance: %.0f ohm (R_REF = %.0f ohm)\n", idResistance, R_REF);
  else
    Serial.printf("Measured resistance: - (R_REF = %.0f ohm)\n", R_REF);
  Serial.printf("Light expects %.0f-%.0f ohm, GPS + environment expects %.0f-%.0f ohm\n",
                R_LIGHT * (1 - R_TOLERANCE), R_LIGHT * (1 + R_TOLERANCE),
                R_GPS_ENV * (1 - R_TOLERANCE), R_GPS_ENV * (1 + R_TOLERANCE));
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
  out += "\",\"resistance_ohm\":";
  out += String(idResistance, 0);
  out += ",\"debug\":";
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

void pressButton()
{
  digitalWrite(togglePin, HIGH);
  delay(PRESS_MS);
  digitalWrite(togglePin, LOW);
}

void handleToggle()
{
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
  pinMode(togglePin, OUTPUT);
  digitalWrite(togglePin, LOW);
  analogSetPinAttenuation(ID_PIN, ADC_11db); // measure the full 0-3.3 V range

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

  // The two ID ranges overlap: a reading in the overlap is treated as the light.
  if (R_LIGHT * (1 + R_TOLERANCE) >= R_GPS_ENV * (1 - R_TOLERANCE) &&
      R_GPS_ENV * (1 + R_TOLERANCE) >= R_LIGHT * (1 - R_TOLERANCE))
    Serial.println("Warning: the light and GPS + environment ID resistor ranges overlap. "
                   "Use resistor values further apart or a smaller R_TOLERANCE.");
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
