#pragma once

#include <lvgl.h>

namespace ui {

struct Callbacks {
  void (*onPrev)();
  void (*onNext)();
  void (*onPlayPause)();
  void (*onPreset)(int key); // 1-based preset slot
  void (*onRecent)(int id);  // RecentTile::id of the tapped album
  void (*onSettingTapped)(int index);
  void (*onWifiSetup)(); // held, not tapped: it restarts the knob into setup mode
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
// Puts the controls away without the fade, so a touch that wakes the screen shows only the art.
void hideControlsNow();
void setControlsTimeout(uint32_t seconds);
void setPlaying(bool playing);
// Thin ring around the edge while only the art is showing. Interpolates between reports.
void setProgress(long positionMs, long durationMs, bool playing);
// Darkens the art while the new track's art is still on its way.
void setArtPending(bool pending);
void setConnected(bool connected); // small dot at the top of the controls

// Battery-shaped icon with a proportional, color-coded fill; tap toggles it to a plain
// percentage (tap again to go back). onUsbPower adds a bolt: charging or already full, which
// this board can't tell apart (see Reading::externalPower in Battery.h).
void setBattery(int percent0to100, bool onUsbPower);

// Settings page: one row per setting, a row selected by tapping it. The knob adjusts the
// selected row while this page is showing (see isSettingsActive()).
void setSettings(const char *const *labels, int count);
void setSettingValue(int index, const char *text);
void setSelectedSetting(int index);
bool isSettingsActive();
void setRecentEnabled(bool enabled);

// Network page: read-only status plus a hold-to-start Wi-Fi setup button.
void setNetworkInfo(const char *ssid, const char *ip, const char *signal, const char *wiim);

// Full-screen Wi-Fi setup instructions, replacing the normal pages (see net::portal).
void showSetup(const char *apName, const char *apPassword, const char *qrPayload, void (*onCancel)());
void setSetupStatus(const char *text);

} // namespace ui
