#pragma once

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

} // namespace battery
} // namespace power
