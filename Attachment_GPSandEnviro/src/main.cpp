#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

Adafruit_BME680 bme;

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

static const float HUM_REFERENCE = 40.0f;                    // ideal relative humidity (%)
static const float HUM_WEIGHT = 0.25f;                       // humidity share of the score
static const unsigned long BURN_IN_MS = 5UL * 60UL * 1000UL; // 5 minutes

static float gasBaseline = 0.0f;
static double burnInSum = 0.0;
static unsigned long burnInCount = 0;
static unsigned long burnInStart = 0;
static bool burnInDone = false;

static const char *iaqRating(float iaq)
{
  if (iaq <= 50)
    return "Excellent";
  if (iaq <= 100)
    return "Good";
  if (iaq <= 150)
    return "Lightly polluted";
  if (iaq <= 200)
    return "Moderately polluted";
  if (iaq <= 250)
    return "Heavily polluted";
  if (iaq <= 350)
    return "Severely polluted";
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

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // Uses the board's default I2C pins (SDA = GPIO8, SCL = GPIO9 on the
  // ESP32-C3-DevKitM-1). Use Wire.begin(SDA, SCL) if wired differently.
  Wire.begin();

  // Most breakout boards use 0x77; some use 0x76.
  if (!bme.begin(0x77) && !bme.begin(0x76))
  {
    Serial.println("Could not find a BME680 sensor, check wiring!");
    while (true)
      delay(1000);
  }

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320 C for 150 ms
}

void loop()
{
  AirQuality aq = calculateAirQuality();

  Serial.printf("T: %.2f C  RH: %.2f %%  P: %.2f hPa  Gas: %.2f kOhm  ",
                aq.temperature, aq.humidity, aq.pressure, aq.gasKOhms);
  if (aq.valid)
    Serial.printf("IAQ: %.0f (%s)\n", aq.iaq, aq.rating);
  else
    Serial.printf("IAQ: -- (%s)\n", aq.rating);

  delay(3000);
}
