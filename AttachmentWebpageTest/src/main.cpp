#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "index_html.h"

#define togglePin 4
#define high 1
#define low 0

// ---- Settings ----
#define USE_ACCESS_POINT true // true: ESP32 makes its own network. false: joins your router.

const char *AP_SSID = "SSID";
const char *AP_PASS = "password"; // at least 8 characters

const char *WIFI_SSID = "your_ssid"; // only used when USE_ACCESS_POINT is false
const char *WIFI_PASS = "your_password";
// ------------------

WebServer server(80);
bool ledState = false;

void handleRoot() { server.send_P(200, "text/html", PAGE); }
void handleState() { server.send(200, "text/plain", ledState ? "ON" : "OFF"); }
/*void handleToggle()
{
  ledState = !ledState;
  digitalWrite(togglePin, ledState);
  server.send(200, "text/plain", ledState ? "ON" : "OFF");
}
*/
// new below
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
  ledState = !ledState;
  server.send(200, "text/plain", ledState ? "ON" : "OFF");
}
// new ^^^

void setup()
{
  Serial.begin(115200);
  pinMode(togglePin, OUTPUT);
  digitalWrite(togglePin, LOW);

#if USE_ACCESS_POINT
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Connect to Wi-Fi '");
  Serial.print(AP_SSID);
  Serial.println("', then open:");
  Serial.println(WiFi.softAPIP()); // 192.168.4.1 by default
#else
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Open in your browser: ");
  Serial.println(WiFi.localIP());
#endif

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/state", handleState);
  server.begin();
}

void loop()
{
  server.handleClient();
}
