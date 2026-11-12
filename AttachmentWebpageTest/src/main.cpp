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
// If the button hasn't been pressed for 5.5 seconds (half second buffer), the lamp's next press
// turns it Off instead of moving to the next mode. Presses after that step
// through the modes from Off again.
#define LIGHT_MODES 6
const unsigned long LIGHT_TIMEOUT_MS = 5.5UL * 1000UL; // 5.5 seconds
int lightMode = 0;
unsigned long lastLightPress = 0; // millis() of the last press (or plug-in)

// ---- Attachment detection ----
// Each attachment has an ID resistor from a USB-C pin to GND. The hub has a
// reference resistor from that pin to 3.3 V, so the voltage on ID_PIN tells
// us the attachment's resistance:
//   3.3V -- R_REF -- ID_PIN -- R_ID (in attachment) -- GND
// Set these to your resistor values. The measured resistance is printed on
// the serial monitor every few seconds to help with this.
#define ID_PIN 5                 // ADC pin wired to the USB-C ID line
const float R_REF = 10000.0;     // reference resistor in the hub (ohms)
const float R_LIGHT = 10000.0;   // ID resistor in the light attachment
const float R_GPS_ENV = 22000.0; // ID resistor in the GPS + environment attachment
const float R_TOLERANCE = 0.20;  // accept +/- 20%

float idResistance = 0; // last measured ID resistance in ohms (0 = nothing plugged in)
const char *attachment = "none";

// ---- GPS + environment attachment ----
// It joins this access point and sends one JSON reading every 3 s by UDP.
const uint16_t ATTACHMENT_UDP_PORT = 4210; // must match RECEIVER_PORT in Attachment_GPSandEnviro
WiFiUDP udp;
String latestReading = "";
unsigned long latestReadingAt = 0;
// ------------------------------

bool near(float value, float target) { return fabs(value - target) <= target * R_TOLERANCE; }

void updateAttachment()
{
  // Average a few readings to reduce noise.
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++)
    mv += analogReadMilliVolts(ID_PIN);
  mv /= 8;

  // Pin pulled up to ~3.3 V means nothing is plugged in.
  if (mv > 3200)
    idResistance = 0;
  else
    idResistance = R_REF * mv / (3300.0 - mv);

  const char *previous = attachment;
  if (idResistance > 0 && near(idResistance, R_LIGHT))
    attachment = "light";
  else if (idResistance > 0 && near(idResistance, R_GPS_ENV))
    attachment = "gps_env";
  else
    attachment = "none";

  // A newly plugged-in lamp starts Off.
  if (attachment != previous && strcmp(attachment, "light") == 0)
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
  }
}

// What the web page asks for every 2 s: which attachment is plugged in and,
// for the GPS + environment attachment, its latest reading.
void handleStatus()
{
  String out = "{\"attachment\":\"";
  out += attachment;
  out += "\",\"resistance_ohm\":";
  out += String(idResistance, 0);
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

  // 5.5 seconds or more since the last press: this press turns the lamp Off.
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
  if (millis() - lastPrint >= 3000)
  {
    lastPrint = millis();
    Serial.printf("ID resistance: %.0f ohm -> %s\n", idResistance, attachment);
  }
}
