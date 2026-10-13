#pragma once

#include <lvgl.h>

namespace ui {

struct Callbacks {
  void (*onPrev)();
  void (*onNext)();
  void (*onPlayPause)();
  void (*onPreset)(int key); // 1-based preset slot
  void (*onRecent)(int id);  // RecentTile::id of the tapped album
};

enum class PresetIcon { kDisc, kHeadphones, kEq, kMusic };

struct PresetTile {
  int key; // passed back through Callbacks::onPreset
  const char *name;
  const char *subtitle;
  PresetIcon icon;
};

struct RecentTile {
  int id; // passed back through Callbacks::onRecent
  const lv_img_dsc_t *thumb; // nullptr shows a blank tile
  bool playing; // red ring
};

void begin(const Callbacks &callbacks);

// Rebuilds the recently-played grid: up to 10 thumbnails in rows of 3-4-3, most recent first.
void setRecent(const RecentTile *tiles, int count);

// Rebuilds the presets screen's tile grid (text is copied, so the caller's strings can go away).
void setPresets(const PresetTile *tiles, int count);

void setTrack(const char *title, const char *artist, const char *album);
// art nullptr shows a black backdrop with the controls always up; tinted is the backdrop used
// while the controls are showing (falls back to art).
void setAlbumArt(const lv_img_dsc_t *art, const lv_img_dsc_t *tinted);
void setVolume(int volume0to100);
// Brings the controls up over the art and restarts their hide timer. For knob turns and wakes,
// which LVGL doesn't see as input.
void showControls();
void setControlsTimeout(uint32_t seconds);
void setPlaying(bool playing);
void setConnected(bool connected); // small dot at the top of the controls

// Battery-shaped icon with a proportional, color-coded fill; tap toggles it to a plain
// percentage (tap again to go back). charging pulses it -- see the caveat on
// Reading::charging in Battery.h about how that's actually detected.
void setBattery(int percent0to100, bool charging);

} // namespace ui
