#pragma once

#include <stdint.h>

namespace power {

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
bool isDimmed();

} // namespace power
