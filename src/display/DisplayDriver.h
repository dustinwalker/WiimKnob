#pragma once

#include <lvgl.h>

namespace display {

// Bring up the QSPI bus, the ST77916 panel and LVGL's display driver. Must be called once
// from setup(), before any UI code runs.
void begin();

// Pump LVGL's timer handler. Call every loop() iteration.
void tick();

// Puts the panel controller into its sleep mode ahead of deep sleep. The panel is reset and
// re-initialised on the next boot, so there's no matching wake.
void sleepPanel();

// Draws pending UI changes right away instead of on LVGL's next refresh tick -- for input that
// should show up with no extra delay (knob turns).
void refreshNow();

void setBacklightOn(bool on);
// 1-100, applied now if the backlight is on and remembered for when it next turns on.
void setBrightness(uint8_t percent);
// A quarter of the set brightness, for the warning before the screen turns off.
void setDimmed(bool dimmed);

} // namespace display
