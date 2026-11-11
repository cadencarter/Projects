#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <TinyGPSPlus.h>

const char *WIFI_SSID = "MyLight";
const char *WIFI_PASS = "12345678";

Adafruit_BME680 bme;
static bool bmeOk = false;

// ---------------------------------------------------------------------------
// GPS (ATGM336H)
// ---------------------------------------------------------------------------
// The ATGM336H sends NMEA sentences over UART at 9600 baud by default.
// Wiring: GPS TX -> GPS_RX_PIN, GPS RX -> GPS_TX_PIN, VCC -> 3V3, GND -> GND.
// USB-C (Serial) is the ESP32-C3's built-in USB CDC port, so the hardware
// UART pins are free for the GPS.
static const int GPS_RX_PIN = 20; // ESP32 receives on this pin
static const int GPS_TX_PIN = 21; // ESP32 transmits on this pin
static const uint32_t GPS_BAUD = 9600;

TinyGPSPlus gps;
HardwareSerial GPSSerial(1);

// How often a combined GPS + environment record is sent over USB-C.
static const unsigned long REPORT_INTERVAL_MS = 3000;

// Reads all bytes waiting from the GPS and passes them to the NMEA parser.
// Call this as often as possible so the UART buffer never overflows.
void updateGPS()
{
  while (GPSSerial.available() > 0)
    gps.encode(GPSSerial.read());
}

// ---------------------------------------------------------------------------
// Air quality
// ---------------------------------------------------------------------------
// The Adafruit library only reports raw gas resistance; Bosch's IAQ index
// needs their closed-source BSEC library. This is an approximation instead:
// the score is 25% humidity (40% RH is ideal) and 75% gas resistance compared
// to a clean-air baseline. VOCs lower the gas resistance, so a lower
// resistance than the baseline means worse air.
//
// The baseline is the average resistance during a burn-in period after power
// on. After that it moves up slowly when the air gets cleaner than the
// baseline. Start the device in clean air for best results.

struct AirQuality
{
  bool valid;         // false while the sensor is warming up or a read failed
  float iaq;          // 0 (excellent) .. 500 (hazardous), IAQ-style scale
  float score;        // 0 (worst) .. 100 (best)
  const char *rating; // human-readable category
  float temperature;  // degrees C
  float humidity;     // %RH
  float pressure;     // hPa
  float gasKOhms;     // gas resistance in kOhms
};

static const float HUM_REFERENCE = 40.0f;           // ideal relative humidity (%)
static const float HUM_WEIGHT = 0.25f;              // humidity share of the score
static const unsigned long BURN_IN_MS = 5UL * 60UL * 1000UL; // 5 minutes

static float gasBaseline = 0.0f;
static double burnInSum = 0.0;
static unsigned long burnInCount = 0;
static unsigned long burnInStart = 0;
static bool burnInDone = false;

static const char *iaqRating(float iaq)
{
  if (iaq <= 50)  return "Excellent";
  if (iaq <= 100) return "Good";
  if (iaq <= 150) return "Lightly polluted";
  if (iaq <= 200) return "Moderately polluted";
  if (iaq <= 250) return "Heavily polluted";
  if (iaq <= 350) return "Severely polluted";
  return "Hazardous";
}

// Takes a reading from the BME680 and returns the current air quality.
// Call it regularly (every few seconds); the first BURN_IN_MS after the first
// call are used to learn the gas baseline and return valid = false.
AirQuality calculateAirQuality()
{
  AirQuality aq = {};
  aq.valid = false;
  aq.rating = "Unknown";

  if (!bmeOk)
  {
    aq.rating = "Sensor not found";
    return aq;
  }

  if (!bme.performReading())
  {
    aq.rating = "Sensor read failed";
    return aq;
  }

  aq.temperature = bme.temperature;
  aq.humidity = bme.humidity;
  aq.pressure = bme.pressure / 100.0f;
  aq.gasKOhms = bme.gas_resistance / 1000.0f;

  const float gas = (float)bme.gas_resistance;

  // Burn-in: average the gas resistance to get the clean-air baseline.
  if (!burnInDone)
  {
    if (burnInStart == 0)
      burnInStart = millis();

    burnInSum += gas;
    burnInCount++;

    if (millis() - burnInStart < BURN_IN_MS)
    {
      aq.rating = "Warming up";
      return aq;
    }

    gasBaseline = burnInSum / burnInCount;
    burnInDone = true;
  }

  // Cleaner air than the baseline: slowly move the baseline up.
  if (gas > gasBaseline)
    gasBaseline += (gas - gasBaseline) * 0.01f;

  // Humidity score: full marks at HUM_REFERENCE, falling off linearly to 0
  // at 0% or 100% RH.
  float humOffset = aq.humidity - HUM_REFERENCE;
  float humScore;
  if (humOffset <= 0)
    humScore = (aq.humidity / HUM_REFERENCE) * (HUM_WEIGHT * 100.0f);
  else
    humScore = ((100.0f - aq.humidity) / (100.0f - HUM_REFERENCE)) * (HUM_WEIGHT * 100.0f);

  // Gas score: full marks at or above the baseline.
  float gasRatio = gas / gasBaseline;
  if (gasRatio > 1.0f)
    gasRatio = 1.0f;
  float gasScore = gasRatio * ((1.0f - HUM_WEIGHT) * 100.0f);

  aq.score = constrain(humScore + gasScore, 0.0f, 100.0f);
  aq.iaq = (100.0f - aq.score) * 5.0f;
  aq.rating = iaqRating(aq.iaq);
  aq.valid = true;
  return aq;
}

// ---------------------------------------------------------------------------
// USB-C output
// ---------------------------------------------------------------------------
// Each report is one line of JSON, so a program on the computer can read it
// line by line. Values that aren't available yet are sent as null.

static void addField(String &out, const char *key, const String &value)
{
  if (out.length() > 1)
    out += ',';
  out += '"';
  out += key;
  out += "\":";
  out += value;
}

static String num(double value, int decimals, bool valid)
{
  return valid ? String(value, decimals) : String("null");
}

static String quoted(const char *value)
{
  return String("\"") + value + "\"";
}

void sendReport(const AirQuality &aq)
{
  String out = "{";

  // GPS
  char timeStr[25];
  bool timeValid = gps.date.isValid() && gps.time.isValid() && gps.date.year() > 2000;
  if (timeValid)
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());

  bool fix = gps.location.isValid() && gps.location.age() < 2000;
  addField(out, "ms", String(millis()));
  addField(out, "utc", timeValid ? quoted(timeStr) : String("null"));
  addField(out, "fix", fix ? "true" : "false");
  addField(out, "lat", num(gps.location.lat(), 6, fix));
  addField(out, "lon", num(gps.location.lng(), 6, fix));
  addField(out, "alt_m", num(gps.altitude.meters(), 1, fix && gps.altitude.isValid()));
  addField(out, "speed_kmh", num(gps.speed.kmph(), 2, fix && gps.speed.isValid()));
  addField(out, "course_deg", num(gps.course.deg(), 1, fix && gps.course.isValid()));
  addField(out, "sats", gps.satellites.isValid() ? String(gps.satellites.value()) : String("null"));
  addField(out, "hdop", num(gps.hdop.hdop(), 2, gps.hdop.isValid()));
  addField(out, "gps_chars", String(gps.charsProcessed()));
  addField(out, "gps_bad_checksums", String(gps.failedChecksum()));

  // Environment
  bool envValid = bmeOk && strcmp(aq.rating, "Sensor read failed") != 0;
  addField(out, "temp_c", num(aq.temperature, 2, envValid));
  addField(out, "humidity_pct", num(aq.humidity, 2, envValid));
  addField(out, "pressure_hpa", num(aq.pressure, 2, envValid));
  addField(out, "gas_kohm", num(aq.gasKOhms, 2, envValid));
  addField(out, "iaq", num(aq.iaq, 0, aq.valid));
  addField(out, "iaq_rating", quoted(aq.rating));

  out += '}';
  Serial.println(out);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // GPS UART. A larger receive buffer stops NMEA data being lost while the
  // BME680 reading blocks the loop for a few hundred milliseconds.
  GPSSerial.setRxBufferSize(1024);
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Uses the board's default I2C pins (SDA = GPIO8, SCL = GPIO9 on the
  // ESP32-C3-DevKitM-1). Use Wire.begin(SDA, SCL) if wired differently.
  Wire.begin();

  // Most breakout boards use 0x77; some use 0x76. If the sensor is missing,
  // keep running so GPS data is still sent.
  bmeOk = bme.begin(0x77) || bme.begin(0x76);
  if (bmeOk)
  {
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320 C for 150 ms
  }
  else
  {
    Serial.println("{\"error\":\"BME680 not found, check wiring\"}");
  }
}

void loop()
{
  static unsigned long lastReport = 0;
  static bool gpsWarningSent = false;

  updateGPS();

  // No NMEA data at all after 5 s means the GPS is not wired or powered.
  if (!gpsWarningSent && millis() > 5000 && gps.charsProcessed() < 10)
  {
    Serial.println("{\"error\":\"No data from GPS, check wiring\"}");
    gpsWarningSent = true;
  }

  if (millis() - lastReport >= REPORT_INTERVAL_MS)
  {
    lastReport = millis();
    AirQuality aq = calculateAirQuality();
    updateGPS();
    sendReport(aq);
  }
}
