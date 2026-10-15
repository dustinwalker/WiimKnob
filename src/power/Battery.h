#pragma once

#include <stdint.h>

namespace power {
namespace battery {

struct Reading {
  float voltage = 0;        // after undoing the sense divider; the supply rail while on USB power
  int percent = 0;          // 0-100 from a generic LiPo curve; frozen at the last battery reading on USB
  float hoursRemaining = 0; // rough estimate; see BATTERY_FULL_RUNTIME_HOURS in Config.h
  bool externalPower = false; // on USB power (charging, or full and topped up)
};

// Configures the ADC pin. Call once from setup().
void begin();

// Takes a fresh reading (averages a few samples internally). Cheap enough to call every few
// seconds; no need to cache it yourself.
Reading read();

// The voltage from the most recent read(), unsmoothed. 0 before the first read.
float lastRawVoltage();

// A curve learned from this battery (see BatteryCal.h): millivolts at 100%, 90%, ... 0%, from a run
// of runMinutes at a steady awake load. Saved to NVS and used instead of the generic LiPo curve.
constexpr int kCurvePoints = 11;
void setLearnedCurve(const uint16_t mv[kCurvePoints], uint32_t runMinutes);
bool hasLearnedCurve();
uint32_t learnedRunMinutes();

} // namespace battery
} // namespace power
