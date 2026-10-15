#pragma once

#include <Arduino.h>

// User-adjustable settings, edited from the knob's settings page and kept in NVS. Each one
// steps through a fixed list of values; defaults come from Config.h.
namespace settings {

enum Item { kBrightness, kControlsTimeout, kScreenTimeout, kSleepTimeout, kRecentPage, kBatteryCal, kCount };

void begin();

int value(Item item); // percent, seconds, seconds, minutes, 0/1
const char *label(Item item);
void format(Item item, char *buf, size_t len);

// Moves item delta steps through its list (clamped). Returns whether the value changed.
bool step(Item item, int delta);
// Sets item to the nearest value in its list, for changes the firmware makes itself.
void set(Item item, int value);
// Writes pending changes now, e.g. right before deep sleep.
void saveNow();

// Writes changed values to NVS once they've been left alone for a couple of seconds, so a knob
// spin doesn't wear the flash with one write per detent.
void service();

} // namespace settings
