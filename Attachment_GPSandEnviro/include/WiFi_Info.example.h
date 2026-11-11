#pragma once

// Copy this file to WiFi_Info.h (same folder) and fill in the network name
// and password of the receiving ESP32's access point.
// WiFi_Info.h is ignored by git so the password is never committed.
// The ESP32-C3 only supports 2.4 GHz WiFi.

const char *WIFI_SSID = "YourNetworkName";
const char *WIFI_PASS = "YourPassword";
