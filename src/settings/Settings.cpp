#include "Settings.h"
#include "Config.h"

#include <Preferences.h>

#ifndef CONTROLS_TIMEOUT_S
#define CONTROLS_TIMEOUT_S 8
#endif

namespace settings {

namespace {

struct Spec {
  const char *key;
  const char *label;
  const int *options;
  int count;
  int fallback;
};

const int kBrightnessOpts[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
const int kControlsOpts[] = {3, 5, 8, 10, 15, 20, 30};
const int kScreenOpts[] = {20, 30, 45, 70, 90, 120, 180, 300};
const int kSleepOpts[] = {2, 5, 10, 15, 30, 60};
const int kOnOffOpts[] = {0, 1};

#define OPTS(a) a, (int)(sizeof(a) / sizeof(a[0]))
const Spec kSpecs[kCount] = {
    {"bright", "Brightness", OPTS(kBrightnessOpts), 70}, // the backlight is the biggest awake draw
    {"ctrl", "Hide controls", OPTS(kControlsOpts), CONTROLS_TIMEOUT_S},
    {"screen", "Screen off", OPTS(kScreenOpts), SCREEN_TIMEOUT_S},
    {"sleep", "Sleep", OPTS(kSleepOpts), DEEP_SLEEP_TIMEOUT_MIN},
    {"recent", "Recently played", OPTS(kOnOffOpts), 0}, // off: Roon and Connect sources leave nothing to save
    {"batcal", "Calibrate battery", OPTS(kOnOffOpts), 0},
};
#undef OPTS

int index[kCount];
uint32_t dirtySinceMs = 0;
constexpr uint32_t kSaveDelayMs = 2000;

int nearestIndex(const Spec &spec, int v) {
  int best = 0;
  for (int i = 1; i < spec.count; i++) {
    if (abs(spec.options[i] - v) < abs(spec.options[best] - v)) best = i;
  }
  return best;
}

} // namespace

void begin() {
  Preferences prefs;
  prefs.begin("wiimknob", true);
  for (int i = 0; i < kCount; i++) {
    index[i] = nearestIndex(kSpecs[i], prefs.getInt(kSpecs[i].key, kSpecs[i].fallback));
  }
  prefs.end();
}

int value(Item item) { return kSpecs[item].options[index[item]]; }

const char *label(Item item) { return kSpecs[item].label; }

void format(Item item, char *buf, size_t len) {
  int v = value(item);
  switch (item) {
  case kBrightness:
    snprintf(buf, len, "%d%%", v);
    break;
  case kSleepTimeout:
    snprintf(buf, len, "%d min", v);
    break;
  case kRecentPage:
  case kBatteryCal:
    snprintf(buf, len, "%s", v ? "On" : "Off");
    break;
  default:
    if (v >= 60 && v % 60 == 0) {
      snprintf(buf, len, "%d min", v / 60);
    } else {
      snprintf(buf, len, "%ds", v);
    }
    break;
  }
}

bool step(Item item, int delta) {
  int next = constrain(index[item] + delta, 0, kSpecs[item].count - 1);
  if (next == index[item]) return false;
  index[item] = next;
  dirtySinceMs = millis() | 1;
  return true;
}

void set(Item item, int v) {
  index[item] = nearestIndex(kSpecs[item], v);
  dirtySinceMs = millis() | 1;
}

void saveNow() {
  if (!dirtySinceMs) return;
  dirtySinceMs = (millis() - kSaveDelayMs) | 1; // due now
  service();
}

void service() {
  if (!dirtySinceMs || millis() - dirtySinceMs < kSaveDelayMs) return;
  dirtySinceMs = 0;
  Preferences prefs;
  prefs.begin("wiimknob", false);
  for (int i = 0; i < kCount; i++) {
    prefs.putInt(kSpecs[i].key, value((Item)i));
  }
  prefs.end();
}

} // namespace settings
