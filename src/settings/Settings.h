#pragma once

#include <Arduino.h>

// User-adjustable settings, edited from the knob's settings page and kept in NVS. Each one
// steps through a fixed list of values; defaults come from Config.h.
namespace settings {

enum Item { kBrightness, kControlsTimeout, kScreenTimeout, kSleepTimeout, kUsbScreenTimeout, kRecentPage, kTuneInPage, kBatteryCal, kRoonZone, kCount };

void begin();

int value(Item item); // percent, seconds, seconds, minutes, minutes (0: never), 0/1
const char *label(Item item);
bool isToggle(Item item); // on/off: a tap flips it
void format(Item item, char *buf, size_t len);
void formatValue(Item item, int value, char *buf, size_t len);

// The fixed list each item steps through, for the web page. key is its NVS key.
const char *key(Item item);
int optionCount(Item item);
int option(Item item, int i);

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
