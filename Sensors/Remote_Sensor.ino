#include "arduino_secrets.h"
/*
  MKR 1010 (Sensors) — 1-minute Cloud publishing + regression-based direction
  FIXES:
    - Wait for first SCD41 sample at boot (warm-up)
    - Keep last valid CO2 sample; publish it even if readMeasurement() wasn't true on that exact loop
    - Mark samples stale after a timeout; otherwise reuse last known good value
    - Small heartbeat LED so you know the loop is alive
*/

#include "thingProperties.h"
#include <SparkFun_SCD4x_Arduino_Library.h>
#include <Arduino_MKRENV.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <RunningMedian.h>
#include <math.h>

// ========= Sampling & Publishing =========
static const uint16_t WINDOW_SIZE     = 60;
static const unsigned long PUBLISH_MS = 60000UL; // 1 minute

// Slope deadbands
static const float TEMP_SLOPE_DEADBAND_C_per_min  = 0.02f;
static const float CO2_SLOPE_DEADBAND_ppm_per_min = 0.5f;

// Staleness control (reuse last valid CO2 for up to this long)
static const unsigned long CO2_STALE_OK_MS = 180000UL; // 3 minutes

// ========= Sensors =========
SCD4x mySensor;

// ========= Rolling stats =========
RunningMedian carbonStats(WINDOW_SIZE);
RunningMedian tempStats(WINDOW_SIZE);

// ========= Time-series buffers for regression =========
struct Sample { unsigned long t_ms; float value; };
Sample bufTemp[WINDOW_SIZE], bufCO2[WINDOW_SIZE];
uint16_t countTemp = 0, countCO2 = 0;
uint16_t headTemp = 0, headCO2 = 0;

// Last known good CO2
static float        lastCO2ppm   = NAN;
static unsigned long lastCO2At   = 0;

// ========= Helpers =========
static void addSample(Sample* buf, uint16_t& head, uint16_t& count, unsigned long t_ms, float v) {
  buf[head].t_ms = t_ms;
  buf[head].value = v;
  head = (head + 1) % WINDOW_SIZE;
  if (count < WINDOW_SIZE) count++;
}

static float linearSlopePerSecond(const Sample* buf, uint16_t head, uint16_t count) {
  if (count < 3) return NAN;
  uint16_t idx = (head + WINDOW_SIZE - count) % WINDOW_SIZE;
  unsigned long t0 = buf[idx].t_ms;

  double sumX=0, sumY=0, sumXX=0, sumXY=0;
  for (uint16_t i = 0; i < count; ++i) {
    const Sample& s = buf[(idx + i) % WINDOW_SIZE];
    double x = (double)(s.t_ms - t0) / 1000.0; // seconds
    double y = (double)s.value;
    sumX += x; sumY += y; sumXX += x*x; sumXY += x*y;
  }
  double n = (double)count;
  double denom = (n*sumXX - sumX*sumX);
  if (denom == 0.0) return NAN;
  return (float)((n*sumXY - sumX*sumY) / denom);
}

static void fillMacAddress(String& out) {
  byte mac[6]; WiFi.macAddress(mac);
  char s[18];
  snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  out = String(s);
}

static void scd41StartClean() {
  mySensor.stopPeriodicMeasurement(); // harmless if not running
  delay(5);
  mySensor.startPeriodicMeasurement();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT); // heartbeat

  Serial.begin(115200);
  delay(1500);

  // Cloud
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();
  fillMacAddress(macAddress);

  // I2C + sensors
  Wire.begin();
  Wire.setClock(100000); // safest with longer chains
  Wire.setTimeout(20);

  if (!mySensor.begin()) {
    Serial.println(F("SCD4x not detected. Check wiring/Qwiic/ESLOV."));
    while (1) { digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); delay(250); }
  }

  scd41StartClean();
  Serial.println(F("[SCD41] Started periodic measurement. Waiting for first sample (~5–10 s)..."));

  // Warm-up: wait up to 15 s for first valid sample
  unsigned long t0 = millis();
  while (millis() - t0 < 15000UL) {
    if (mySensor.readMeasurement()) {
      float c = mySensor.getCO2();
      if (isfinite(c) && c > 0) {
        lastCO2ppm = c;
        lastCO2At  = millis();
        carbonStats.add(c);
        addSample(bufCO2, headCO2, countCO2, lastCO2At, c);
        Serial.print(F("[SCD41] First CO2=")); Serial.print(c); Serial.println(F(" ppm"));
        break;
      }
    }
    delay(200);
  }

  if (!ENV.begin()) {
    Serial.println(F("MKR ENV shield not detected. Continuing without it."));
  }
}

void loop() {
  ArduinoCloud.update();
  unsigned long now = millis();

  // Heartbeat
  static unsigned long lastBeat = 0;
  if (now - lastBeat >= 500) {
    lastBeat = now;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // ===== Read ENV (fast) =====
  float tC   = ENV.readTemperature();
  float hum  = ENV.readHumidity();
  float pres = ENV.readPressure();
  float lux  = ENV.readIlluminance();

  // Update temp rolling/regression even if Cloud publish isn't due
  if (isfinite(tC)) {
    tempStats.add(tC);
    addSample(bufTemp, headTemp, countTemp, now, tC);
  }

  // ===== Read SCD41 when fresh =====
  // We ALWAYS keep last valid sample for later publishing
  if (mySensor.readMeasurement()) {
    float c = mySensor.getCO2();
    if (isfinite(c) && c > 0) {
      lastCO2ppm = c;
      lastCO2At  = now;

      carbonStats.add(c);
      addSample(bufCO2, headCO2, countCO2, now, c);
    }
  }

  // ===== Precompute stats =====
  float meanT   = (tempStats.getCount() >= 1) ? tempStats.getAverage() : NAN;
  float medT    = (tempStats.getCount() >= 1) ? tempStats.getMedian()  : NAN;
  float meanCO2 = (carbonStats.getCount() >= 1) ? carbonStats.getAverage() : NAN;
  float medCO2  = (carbonStats.getCount() >= 1) ? carbonStats.getMedian()  : NAN;

  // ===== Regression slopes (per minute) =====
  float slopeT_sec   = linearSlopePerSecond(bufTemp, headTemp, countTemp);
  float slopeCO2_sec = linearSlopePerSecond(bufCO2, headCO2, countCO2);
  float slopeT_min   = isnan(slopeT_sec)   ? NAN : (slopeT_sec * 60.0f);
  float slopeCO2_min = isnan(slopeCO2_sec) ? NAN : (slopeCO2_sec * 60.0f);

  // ===== Publish gate =====
  static unsigned long lastPublish = 0;
  if (now - lastPublish >= PUBLISH_MS) {
    lastPublish = now;

    // Direction from regression sign with deadband
    if (!isnan(slopeT_min)) {
      if (slopeT_min >  TEMP_SLOPE_DEADBAND_C_per_min)      temp_Dir = true;
      else if (slopeT_min < -TEMP_SLOPE_DEADBAND_C_per_min) temp_Dir = false;
    }
    if (!isnan(slopeCO2_min)) {
      if (slopeCO2_min >  CO2_SLOPE_DEADBAND_ppm_per_min)       carbon_Dir = true;
      else if (slopeCO2_min < -CO2_SLOPE_DEADBAND_ppm_per_min)  carbon_Dir = false;
    }

    // Push latest ENV values
    if (isfinite(tC))   temperature   = tC;
    if (isfinite(hum))  humidity      = hum;
    if (isfinite(pres)) pressure      = pres;
    if (isfinite(lux))  brightness    = lux;

    // Push CO2: use last valid sample if it's not too old
    if (isfinite(lastCO2ppm) && (now - lastCO2At) <= CO2_STALE_OK_MS) {
      carbon = lastCO2ppm;
    }

    // Medians/means
    if (isfinite(medT))    median_Temp   = medT;
    if (isfinite(meanT))   mean_Temp     = meanT;
    if (isfinite(medCO2))  median_Carbon = medCO2;
    if (isfinite(meanCO2)) mean_Carbon   = meanCO2;

    // Debug
    Serial.print(F("[Publish] T=")); Serial.print(tC, 3);
    Serial.print(F(" C  CO2(last)=")); Serial.print(lastCO2ppm, 1);
    Serial.print(F(" ppm  age=")); Serial.print((now - lastCO2At)/1000.0, 1); Serial.print(F("s"));
    Serial.print(F("  slopeT="));  Serial.print(slopeT_min, 4);  Serial.print(F(" C/min"));
    Serial.print(F("  slopeCO2="));Serial.print(slopeCO2_min, 3);Serial.print(F(" ppm/min"));
    Serial.print(F("  temp_Dir="));Serial.print(temp_Dir ? "↑" : "↓");
    Serial.print(F("  carbon_Dir=")); Serial.println(carbon_Dir ? "↑" : "↓");
  }
}

// ====== Cloud callbacks (stubs) ======
void onMacAddressChange()   {}
void onCarbonChange()       {}
void onCarbon_DirChange()   {}
void onTemp_DirChange()     {}
void onMedian_CarbonChange(){}
void onMedian_TempChange()  {}
void onMean_CarbonChange()  {}
void onMean_TempChange()    {}
void onBrightnessChange()   {}
void onHumidityChange()     {}
void onPressureChange()     {}
void onTemperatureChange()  {}
