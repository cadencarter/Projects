/* Visual test derived from LuxProtocol/AI Test/src/main.cpp, ea4ecf8.
 * Same board, I2C connection, person class, threshold and D0 alert.
 * Image and results come from ONE INVOKE event. No model is flashed.
 */

// to view output: & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m http.server 8000 --bind 127.0.0.1 --directory viewer

#include <Arduino.h>
#include <Seeed_Arduino_SSCMA.h>
#include <ArduinoJson.h>

SSCMA AI;
const int PERSON_CLASS_ID = 0;
const int CONFIDENCE_THRESHOLD = 60;
const int ALERT_PIN = D0;
const unsigned long POLL_INTERVAL_MS = 200;
const unsigned long RESPONSE_TIMEOUT_MS = 15000;
// Bounds apply to the complete JSON response, including base64 JPEG.
const size_t MAX_RESPONSE = 64 * 1024;
static char responseBuffer[MAX_RESPONSE + 1];
static StaticJsonDocument<16384> response;
unsigned long lastPoll = 0;
unsigned long sequence = 0;
bool ready = false;
const char *lastError = "Capture stopped. Reset XIAO to retry.";

void reportError(const char *message)
{
  lastError = message;
  digitalWrite(ALERT_PIN, LOW);
  Serial.print("{\"kind\":\"error\",\"message\":\"");
  Serial.print(message); // Only fixed strings from this source, never module text.
  Serial.println("\"}");
}

// Use the library's I2C transport, but own the image response storage.
// SSCMA 1.0.0's synchronous invoke has a short timeout and frees its
// zero-copy JSON backing buffer before consuming the parsed image.
// This buffer stays alive through parsing AND USB serialization.
bool capture()
{
  const char command[] = "AT+INVOKE=1,0,0\r\n"; // once, unfiltered, include JPEG
  if (AI.write(command, sizeof(command) - 1) != sizeof(command) - 1)
  {
    reportError("Could not send image request. Reset XIAO to retry.");
    return false;
  }
  size_t used = 0;
  bool collecting = false;
  const unsigned long started = millis();
  while (millis() - started < RESPONSE_TIMEOUT_MS)
  {
    const int available = AI.available();
    if (available <= 0)
    {
      delay(1);
      continue;
    }
    char chunk[128];
    const int wanted = min(available, static_cast<int>(sizeof(chunk)));
    const int received = AI.read(chunk, wanted);
    if (received <= 0 || received > wanted)
    {
      reportError("Camera read failed. Check I2C and reset XIAO.");
      return false;
    }
    for (int i = 0; i < received; ++i)
    {
      const char c = chunk[i];
      if (!collecting)
      {
        if (c != '{')
          continue;
        collecting = true;
        used = 0;
      }
      if (c != '\n')
      {
        if (used == MAX_RESPONSE)
        {
          reportError("Image response exceeds 64 KiB. Use a smaller camera image and reset XIAO.");
          return false;
        }
        responseBuffer[used++] = c;
        continue;
      }
      responseBuffer[used] = '\0';
      collecting = false;
      response.clear();
      const auto error = deserializeJson(response, responseBuffer);
      if (error)
      {
        reportError("Invalid or oversized camera JSON. Reset XIAO to retry.");
        return false;
      }
      if (strcmp(response["name"] | "", "INVOKE") != 0)
        continue;
      if ((response["code"] | -1) != 0)
      {
        reportError("Module rejected inference. Check the flashed model and reset XIAO.");
        return false;
      }
      if ((response["type"] | -1) != 1)
        continue; // skip command acknowledgement
      JsonObject data = response["data"].as<JsonObject>();
      if (data.isNull())
      {
        reportError("Inference event has no data. Reset XIAO to retry.");
        return false;
      }
      bool personDetected = false;
      for (JsonArray box : data["boxes"].as<JsonArray>())
      {
        if (box.size() >= 6 && box[5].as<int>() == PERSON_CLASS_ID &&
            box[4].as<int>() >= CONFIDENCE_THRESHOLD)
          personDetected = true;
      }
      digitalWrite(ALERT_PIN, personDetected ? HIGH : LOW);
      response["kind"] = "frame";
      response["sequence"] = ++sequence;
      response["person"] = personDetected;
      response["person_class"] = PERSON_CLASS_ID;
      response["threshold"] = CONFIDENCE_THRESHOLD;
      if (response.overflowed())
      {
        reportError("Too many inference results for JSON memory. Reset XIAO.");
        return false;
      }
      serializeJson(response, Serial);
      Serial.println();
      return true;
    }
  }
  reportError("Camera timed out after 15 seconds. Check power/camera/I2C and reset XIAO.");
  return false;
}

void setup()
{
  Serial.begin(115200);
  pinMode(ALERT_PIN, OUTPUT);
  digitalWrite(ALERT_PIN, LOW);
  const unsigned long started = millis();
  while (!Serial && millis() - started < 5000)
    delay(10);
  ready = AI.begin();
  if (!ready)
    reportError("Cannot connect to Grove Vision AI V2. Check I2C and reset XIAO.");
}

void loop()
{
  if (!ready)
  {
    // Repeat so a viewer connected after boot still sees the failure.
    reportError(lastError);
    delay(3000);
    return;
  }
  // Request/response prevents unbounded USB buffering. Viewer requests 'n'.
  if (!Serial.available())
  {
    delay(1);
    return;
  }
  const char command = Serial.read();
  if (command != 'n')
    return;
  const unsigned long elapsed = millis() - lastPoll;
  if (elapsed < POLL_INTERVAL_MS)
    delay(POLL_INTERVAL_MS - elapsed);
  lastPoll = millis();
  ready = capture(); // Stop on errors: never pair a late event with a new request.
}
