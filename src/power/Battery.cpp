#include "Battery.h"
#include "Config.h"

#include <Arduino.h>
#include <Preferences.h>

namespace power {
namespace battery {

namespace {

// A generic LiPo open-circuit-voltage discharge curve -- not specific to this battery, and it
// drifts under load and with temperature. This board has no fuel-gauge chip, so it's the best
// available without one; see BATTERY_DIVIDER_RATIO in Config.h for the other half of the
// calibration this needs.
struct CurvePoint {
  float voltage;
  int percent;
};
constexpr CurvePoint kCurve[] = {
    {4.20f, 100}, {4.06f, 90}, {3.98f, 80}, {3.92f, 70}, {3.87f, 60},
    {3.82f, 50},  {3.79f, 40}, {3.77f, 30}, {3.74f, 20}, {3.68f, 10},
    {3.45f, 5},   {3.00f, 0},
};
constexpr int kGenericPoints = sizeof(kCurve) / sizeof(kCurve[0]);

uint16_t learned[kCurvePoints] = {}; // mV at 100, 90, ... 0%
bool haveLearned = false;
uint32_t learnedMinutes = 0;
constexpr uint16_t kDefaultCutoffMv = 3450; // ~5% on the generic curve
uint16_t cutoffMv = kDefaultCutoffMv;

int learnedPercent(float voltage) {
  uint16_t mv = (uint16_t)(voltage * 1000);
  if (mv >= learned[0]) return 100;
  if (mv <= learned[kCurvePoints - 1]) return 0;
  for (int k = 0; k < kCurvePoints - 1; k++) {
    if (mv <= learned[k] && mv >= learned[k + 1]) {
      int span = learned[k] - learned[k + 1];
      int frac = span ? (mv - learned[k + 1]) * 10 / span : 0;
      return (kCurvePoints - 2 - k) * 10 + frac;
    }
  }
  return 0;
}

int voltageToPercent(float voltage) {
  if (haveLearned) return learnedPercent(voltage);
  if (voltage >= kCurve[0].voltage) {
    return 100;
  }
  if (voltage <= kCurve[kGenericPoints - 1].voltage) {
    return 0;
  }
  for (int i = 0; i < kGenericPoints - 1; i++) {
    if (voltage <= kCurve[i].voltage && voltage >= kCurve[i + 1].voltage) {
      float span = kCurve[i].voltage - kCurve[i + 1].voltage;
      float t = (voltage - kCurve[i + 1].voltage) / span;
      return kCurve[i + 1].percent + (int)(t * (kCurve[i].percent - kCurve[i + 1].percent));
    }
  }
  return 0;
}

// Measured on real hardware: raw readings jitter by as much as 90mV between samples (8x
// averaged ADC or not), so the percentage comes from an EMA-smoothed voltage.
constexpr float kEmaAlpha = 0.25f;
float filteredVoltage = -1;

float smooth(float rawVoltage) {
  if (filteredVoltage < 0) {
    filteredVoltage = rawVoltage; // seed on first call
  } else {
    filteredVoltage = kEmaAlpha * rawVoltage + (1.0f - kEmaAlpha) * filteredVoltage;
  }
  return filteredVoltage;
}

// GPIO1's divider reads 4.66-4.84V on USB power and 3.9-4.2V on battery (measured), so it sits
// on the board's supply rail rather than the cell: anything above a full cell means USB power.
// Hysteresis keeps ADC jitter (~90mV) from flapping between the two.
constexpr float kExternalOnVolts = 4.45f;
constexpr float kExternalOffVolts = 4.35f;
bool externalPower = false;
int lastBatteryPercent = -1;
float lastRaw = 0;

} // namespace

void begin() {
  Preferences prefs;
  prefs.begin("battery", true);
  haveLearned = prefs.getBytes("curve", learned, sizeof(learned)) == sizeof(learned);
  learnedMinutes = prefs.getUInt("runmin", 0);
  cutoffMv = prefs.getUShort("cutoff", kDefaultCutoffMv);
  prefs.end();
  analogReadResolution(12);
  pinMode(BATTERY_ADC_PIN, INPUT);
}

Reading read() {
  constexpr int kSamples = 8;
  uint32_t sumMilliVolts = 0;
  for (int i = 0; i < kSamples; i++) {
    sumMilliVolts += analogReadMilliVolts(BATTERY_ADC_PIN);
  }
  float sensedVolts = (sumMilliVolts / (float)kSamples) / 1000.0f;
  float rawVoltage = sensedVolts * BATTERY_DIVIDER_RATIO;

  lastRaw = rawVoltage;
  bool wasExternal = externalPower;
  externalPower = rawVoltage > (externalPower ? kExternalOffVolts : kExternalOnVolts);

  Reading r;
  r.externalPower = externalPower;
  if (externalPower) {
    // The rail voltage says nothing about the cell, so hold the last battery-side estimate.
    r.voltage = rawVoltage;
    r.percent = lastBatteryPercent >= 0 ? lastBatteryPercent : 100;
  } else {
    if (wasExternal) filteredVoltage = -1; // reseed rather than drift down from the rail voltage
    r.voltage = smooth(rawVoltage);
    r.percent = voltageToPercent(r.voltage);
    lastBatteryPercent = r.percent;
  }
  r.hoursRemaining = (r.percent / 100.0f) * BATTERY_FULL_RUNTIME_HOURS;
  return r;
}

float lastRawVoltage() { return lastRaw; }

void setLearnedCurve(const uint16_t mv[kCurvePoints], uint32_t runMinutes) {
  memcpy(learned, mv, sizeof(learned));
  haveLearned = true;
  learnedMinutes = runMinutes;
  Preferences prefs;
  prefs.begin("battery", false);
  prefs.putBytes("curve", learned, sizeof(learned));
  prefs.putUInt("runmin", runMinutes);
  prefs.end();
}

bool hasLearnedCurve() { return haveLearned; }

uint32_t learnedRunMinutes() { return learnedMinutes; }

float lowCutoffVolts() { return cutoffMv / 1000.0f; }

void setLearnedCutoff(uint16_t mv) {
  cutoffMv = mv;
  Preferences prefs;
  prefs.begin("battery", false);
  prefs.putUShort("cutoff", mv);
  prefs.end();
}

} // namespace battery
} // namespace power
