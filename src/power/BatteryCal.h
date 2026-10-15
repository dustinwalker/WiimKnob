#pragma once

#include <Arduino.h>

// Learns this battery's voltage-to-percent curve from one full discharge at a steady load (the
// knob held awake, screen on). Started from the Settings page with a full battery; finished by
// the low-battery shutdown, which then means 0%.
namespace power {
namespace batcal {

enum class Result { kNone, kAborted, kFinished };

// Mirrors the Settings row. Turning it off throws away a run in progress.
void setEnabled(bool on);
bool enabled();

// Call on every battery reading. Logs one sample a minute while enabled and on battery. USB power
// finishes a run that already reached near-empty (kFinished) and aborts one that hadn't
// (kAborted); either way the Settings row then needs turning off.
Result sample(float smoothedVolts, bool externalPower);

// True when the log shows the battery had nearly run out and it now reads well above that: the
// run ended in a reset (a brownout under a Wi-Fi peak) and the knob was recharged since.
bool endedEarlier(float volts, bool externalPower);

// Turns the log into a curve and saves it (see battery::setLearnedCurve). False if the run was
// too short to use.
bool finish();

uint32_t loggedMinutes();

} // namespace batcal
} // namespace power
