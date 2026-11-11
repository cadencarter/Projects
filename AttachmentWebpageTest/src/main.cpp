#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "index_html.h"

#define togglePin 4
#define high 1
#define low 0

const char *AP_SSID = "SSID";
const char *AP_PASS = "password"; // at least 8 characters

WebServer server(80);
bool ledState = false;

void handleRoot() { server.send_P(200, "text/html", PAGE); }
void handleState() { server.send(200, "text/plain", ledState ? "ON" : "OFF"); }

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

void setup()
{
  Serial.begin(115200);
  pinMode(togglePin, OUTPUT);
  digitalWrite(togglePin, LOW);

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Connect to Wi-Fi '");
  Serial.print(AP_SSID);
  Serial.println("', then open:");
  Serial.println(WiFi.softAPIP()); // 192.168.4.1 by default

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/state", handleState);
  server.begin();
}

void loop()
{
  server.handleClient();
}
