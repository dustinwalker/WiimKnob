#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace power {

// Why this boot happened, and what the previous deep sleep looked like (if there was one).
struct BootInfo {
  const char *resetReason = "";  // "power-on", "deep-sleep wake", "crash", "brownout", ...
  const char *wakeSource = "";   // "knob", "touch" or "none"
  int64_t sleptSeconds = -1;     // -1: no deep sleep recorded (fresh power-on)
  float sleepVolts = 0;          // battery voltage as it went to sleep
  const char *sleepReason = "";
};
const BootInfo &lastBoot();

// Appends this boot to a 16-entry log in NVS, which (unlike lastBoot()) survives a flat battery.
// Call once, after the first battery reading.
void recordBoot(float bootVolts);
String bootLogJson(); // newest first

// Deep sleep right away (e.g. on a low battery). Never returns.
void sleepNow(const char *reason);

// Runs just before deep sleep, after the backlight is off (e.g. to save state to flash).
void setBeforeSleep(void (*cb)());

// Starts the inactivity clock. Call once from setup(), after display::begin().
void begin();

// Call whenever the user does something (touch, knob turn). Resets the inactivity clock and,
// if the screen had timed out, turns the backlight back on.
void noteActivity();

// Call every loop() iteration. Turns the backlight off after the screen timeout, and puts the
// board into deep sleep after the sleep timeout. Deep sleep never returns -- the board reboots
// into setup() when a wake source fires.
void service();

// Defaults are SCREEN_TIMEOUT_S and DEEP_SLEEP_TIMEOUT_MIN from Config.h.
void setScreenTimeout(uint32_t seconds);
void setSleepTimeout(uint32_t minutes);

// On USB power the board stays awake and the screen dims instead of turning off; the timeouts
// resume from the moment it's unplugged.
void setExternalPower(bool on);

// Whether the backlight is currently on. Useful for callers that want to skip work that only
// matters while something is actually visible (see main.cpp's loop(), which stops pumping
// LVGL -- and therefore stops reading touch through it -- while this is false).
bool isScreenOn();
// Called as the backlight turns on or off, before anything draws.
void setScreenCallback(void (*cb)(bool on));
bool isDimmed();

} // namespace power
