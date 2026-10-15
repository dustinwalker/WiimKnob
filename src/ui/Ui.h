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
  void (*onLibraryItem)(int tab, int index);
  void (*onLibraryTab)(int tab);
  void (*onShuffle)();
  void (*onRepeat)();
};

// Library page tabs, in order.
constexpr int kLibraryHistory = 0;
constexpr int kLibraryPlaylists = 1;
constexpr int kLibraryRadio = 2;
constexpr int kLibraryQueue = 3;

struct LibraryRow {
  const char *title;
  const char *subtitle;
  const lv_img_dsc_t *thumb; // nullptr shows a blank square until setLibraryThumb()
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

void setTrack(const char *title, const char *artist);
// art nullptr shows a black backdrop with the controls always up; tinted is the backdrop used
// while the controls are showing (falls back to art). accent (0xRRGGBB) colors the progress ring.
void setAlbumArt(const lv_img_dsc_t *art, const lv_img_dsc_t *tinted, uint32_t accent = 0xFFFFFF);
void setVolume(int volume0to100);
// Brings the controls up over the art and restarts their hide timer. For knob turns and wakes,
// which LVGL doesn't see as input.
void showControls();
// Puts the controls away without the fade, so a touch that wakes the screen shows only the art.
void hideControlsNow();
// While connecting at boot: the line over the art (or the title, with no art yet). The first
// real track replaces it.
void setBootStatus(const char *text);
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
// Amber battery outline while a battery calibration run is logging.
void setBatteryCalibrating(bool on);

// Settings page: one row per setting, a row selected by tapping it. The knob adjusts the
// selected row while this page is showing (see isSettingsActive()).
void setSettings(const char *const *labels, int count);
void setSettingValue(int index, const char *text);
void setSelectedSetting(int index);
bool isSettingsActive();
void setRecentEnabled(bool enabled);

// Library page (Roon history, playlists and radio): hidden until Roon has approved the knob.
void setLibraryEnabled(bool enabled);
void setLibraryRows(int tab, const LibraryRow *rows, int count); // text is copied
void setLibraryThumb(int tab, int index, const lv_img_dsc_t *thumb);
void showNowPlaying();

// Shuffle and repeat beside the battery icon, shown while Roon is the source. repeat: 0 off, 1 all, 2 one.
void setRoonModes(bool show, bool shuffle, int repeat);

// Network page: read-only status plus a hold-to-start Wi-Fi setup button.
void setNetworkInfo(const char *ssid, const char *ip, const char *signal, const char *wiim);

// Full-screen Wi-Fi setup instructions, replacing the normal pages (see net::portal).
void showSetup(const char *apName, const char *apPassword, const char *qrPayload, void (*onCancel)());
void setSetupStatus(const char *text);

// Full-screen notice (e.g. low battery), replacing the normal pages.
void showMessage(const char *title, const char *body);

} // namespace ui
