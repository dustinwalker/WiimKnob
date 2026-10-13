#pragma once

#include <Arduino.h>

// User-adjustable settings, edited from the knob's settings page and kept in NVS. Each one
// steps through a fixed list of values; defaults come from Config.h.
namespace settings {

enum Item { kBrightness, kControlsTimeout, kScreenTimeout, kSleepTimeout, kCount };

void begin();

int value(Item item); // percent, seconds, seconds, minutes
const char *label(Item item);
void format(Item item, char *buf, size_t len);

// Moves item delta steps through its list (clamped). Returns whether the value changed.
bool step(Item item, int delta);

// Writes changed values to NVS once they've been left alone for a couple of seconds, so a knob
// spin doesn't wear the flash with one write per detent.
void service();

} // namespace settings
