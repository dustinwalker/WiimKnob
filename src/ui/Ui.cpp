#include "Ui.h"

// Generated from Montserrat-Medium.ttf + the same FontAwesome symbol subset LVGL's own
// built-in fonts use, but with the main range extended to 0x20-0x7F,0xA0-0x17F,0x2022 instead
// of just ASCII -- see src/ui/fonts/ for the command. The stock lv_font_montserrat_16/20 only
// cover ASCII plus the icon symbols, so any accented character (e.g. the O with diaeresis in
// "Magnus Ostrom") had no glyph to draw at all. Only used where real track/artist text shows
// up; the icon buttons and the volume number stay on the stock fonts.
LV_FONT_DECLARE(lv_font_montserrat_20_latin);
LV_FONT_DECLARE(lv_font_montserrat_16_latin);
// Just the four FontAwesome glyphs the preset tiles use -- see the header of the .c file.
LV_FONT_DECLARE(lv_font_preset_icons_26);
// Prev/play/pause/next only, sized for the media controls over the art.
LV_FONT_DECLARE(lv_font_media_icons_36);
// Digits 0-9 only, for the big volume readout on the full-screen art page.
LV_FONT_DECLARE(lv_font_montserrat_96_digits);

namespace ui {

namespace {

Callbacks callbacks;

// Red is reserved for "this is adjustable/live": the volume ring, plus the "last tapped"
// markers on the presets/recent screens. Everything else is neutral (dark badges, white icons)
// per the reference look.
constexpr uint32_t kAccentColor = 0xE53935;
constexpr uint32_t kButtonColor = 0x2a2a2a;
constexpr uint32_t kBadgeColor = 0x333333;
constexpr uint32_t kBatteryGreen = 0x639922;

// Swipe order, left to right. Boots on now playing.
constexpr int kNowPlaying = 0;
constexpr int kRecent = 1;
constexpr int kPresets = 2;
constexpr int kSettings = 3;
constexpr int kNetwork = 4;
constexpr int kScreenCount = 5;

lv_obj_t *screens[kScreenCount];
bool pageEnabled[kScreenCount] = {true, true, true, true, true};
lv_obj_t *pageDots[kScreenCount][kScreenCount] = {};
int activeScreen = kNowPlaying;

// Now playing: full-screen art. A tap, a knob turn or waking the screen brings the controls up
// over it, and they hide again after controlsTimeoutMs without input.
lv_obj_t *artImg = nullptr;   // under the controls: the tinted backdrop
lv_obj_t *frontImg = nullptr; // over them: the plain art, faded in as the controls hide
const lv_img_dsc_t *plainArt = nullptr;
const lv_img_dsc_t *tintedArt = nullptr; // backdrop while the controls are up
lv_obj_t *controlsLayer = nullptr;
lv_obj_t *progressArc = nullptr;
long progressBaseMs = 0, progressDurationMs = 0;
uint32_t progressBaseTick = 0;
bool progressPlaying = false;
lv_obj_t *skipFlash = nullptr; // big prev/next icon after a swipe on the art
lv_obj_t *skipFlashIcon = nullptr;
lv_obj_t *skipSpinner = nullptr;
bool skipPending = false; // waiting for the WiiM to report the track a skip asked for
uint32_t skipStartedMs = 0;
constexpr uint32_t kSkipWaitMaxMs = 6000;
lv_obj_t *tickerPill = nullptr; // "title · artist" left over the art while the controls are hidden
lv_obj_t *tickerLabel = nullptr;
bool controlsShown = true;
uint32_t controlsTimeoutMs = 8000;

lv_obj_t *volumeArc = nullptr;
lv_obj_t *volumeLabel = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *artistLabel = nullptr;
lv_obj_t *albumLabel = nullptr;
lv_obj_t *playPauseIcon = nullptr;
lv_obj_t *statusDot = nullptr;
lv_obj_t *batteryBadge = nullptr;      // clickable: taps toggle icon <-> percent
lv_obj_t *batteryIconOutline = nullptr;
lv_obj_t *batteryIconNub = nullptr;
lv_obj_t *batteryFill = nullptr;
lv_obj_t *batteryPercentLabel = nullptr;
lv_obj_t *batteryBolt = nullptr;
bool showingBatteryPercent = false;

// Recently played.
constexpr int kMaxRecentTiles = 10;
constexpr int kRecentThumbSize = 68;
lv_obj_t *recentTiles[kMaxRecentTiles] = {};
int recentTileCount = 0;
lv_obj_t *recentEmptyLabel = nullptr;

// Presets.
lv_obj_t *presetGrid = nullptr;
lv_obj_t *presetEmptyLabel = nullptr;
lv_obj_t *lastFiredTile = nullptr; // red border: last preset fired from here, not "active" --
                                   // the WiiM doesn't report which preset is currently in effect
int lastFiredKey = 0;               // survives a grid rebuild

// Settings.
constexpr int kMaxSettings = 6;
lv_obj_t *settingRows[kMaxSettings] = {};
lv_obj_t *settingValues[kMaxSettings] = {};
int settingCount = 0;

// Network.
lv_obj_t *netValues[4] = {};

// Wi-Fi setup.
lv_obj_t *setupStatus = nullptr;
void (*setupCancel)() = nullptr;

constexpr int kBattIconW = 32, kBattIconH = 18;
constexpr int kBattFillMaxW = kBattIconW - 6; // inset from the outline's border

void startSkip(bool next);

void onPrevClicked(lv_event_t *e) {
  (void)e;
  startSkip(false);
  if (callbacks.onPrev) callbacks.onPrev();
}

void onNextClicked(lv_event_t *e) {
  (void)e;
  startSkip(true);
  if (callbacks.onNext) callbacks.onNext();
}

void onPlayPauseClicked(lv_event_t *e) {
  (void)e;
  if (callbacks.onPlayPause) callbacks.onPlayPause();
}

void onBatteryBadgeClicked(lv_event_t *e) {
  (void)e;
  showingBatteryPercent = !showingBatteryPercent;
  if (showingBatteryPercent) {
    lv_obj_add_flag(batteryIconOutline, LV_OBJ_FLAG_HIDDEN); // also hides its batteryFill child
    lv_obj_add_flag(batteryIconNub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(batteryPercentLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(batteryIconOutline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(batteryIconNub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(batteryPercentLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void goToScreen(int index, bool forward) {
  while (index >= 0 && index < kScreenCount && !pageEnabled[index]) index += forward ? 1 : -1;
  if (index < 0 || index >= kScreenCount || index == activeScreen) {
    return;
  }
  lv_scr_load_anim(screens[index], forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                    220, 0, false);
  activeScreen = index;
}

void setArtOn(lv_obj_t *img, const lv_img_dsc_t *art, int coverSize);

constexpr uint32_t kControlsFadeMs = 500;

void applyArt() {
  setArtOn(artImg, tintedArt ? tintedArt : plainArt, 360);
  setArtOn(frontImg, plainArt, 360);
  if (controlsShown) lv_obj_add_flag(frontImg, LV_OBJ_FLAG_HIDDEN);
}

void frontImgOpaCb(void *img, int32_t v) { lv_obj_set_style_img_opa((lv_obj_t *)img, v, 0); }

void updateProgressArc() {
  if (progressDurationMs <= 0) return;
  long pos = progressBaseMs + (progressPlaying ? (long)lv_tick_elaps(progressBaseTick) : 0);
  if (pos > progressDurationMs) pos = progressDurationMs;
  lv_arc_set_value(progressArc, (int)(pos * 1000 / progressDurationMs));
}

void progressTimerCb(lv_timer_t *) {
  if (!lv_obj_has_flag(progressArc, LV_OBJ_FLAG_HIDDEN)) updateProgressArc();
}

void onControlsFadedOut(lv_anim_t *) {
  lv_obj_add_flag(controlsLayer, LV_OBJ_FLAG_HIDDEN);
  if (progressDurationMs > 0) {
    updateProgressArc();
    lv_obj_clear_flag(progressArc, LV_OBJ_FLAG_HIDDEN);
  }
  if (lv_label_get_text(tickerLabel)[0]) lv_obj_clear_flag(tickerPill, LV_OBJ_FLAG_HIDDEN);
}

void setControlsShown(bool shown) {
  // With no art there's nothing to look at but the controls, so they stay up.
  if (!shown && !plainArt) return;
  if (shown == controlsShown) return;
  controlsShown = shown;
  lv_anim_del(frontImg, frontImgOpaCb);
  if (shown) {
    lv_obj_add_flag(tickerPill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progressArc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(frontImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(controlsLayer, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  // Only the image's own opacity animates, which LVGL blends directly; fading the controls
  // layer itself would need a full-screen intermediate layer render every frame.
  lv_obj_set_style_img_opa(frontImg, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(frontImg, LV_OBJ_FLAG_HIDDEN);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, frontImg);
  lv_anim_set_exec_cb(&a, frontImgOpaCb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&a, kControlsFadeMs);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_ready_cb(&a, onControlsFadedOut);
  lv_anim_start(&a);
}

void controlsTimerCb(lv_timer_t *) {
  if (controlsShown && lv_disp_get_inactive_time(NULL) > controlsTimeoutMs) {
    setControlsShown(false);
  }
}

void onArtTapped(lv_event_t *e) {
  (void)e;
  lv_indev_t *indev = lv_indev_get_act();
  if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) {
    return; // end of a swipe to another screen, not a tap
  }
  setControlsShown(!controlsShown);
}

void endSkip() {
  if (!skipPending) return;
  skipPending = false;
  lv_obj_set_style_text_opa(titleLabel, LV_OPA_COVER, 0);
  lv_obj_set_style_text_opa(artistLabel, LV_OPA_COVER, 0);
  lv_obj_add_flag(skipSpinner, LV_OBJ_FLAG_HIDDEN);
  if (!lv_obj_has_flag(skipFlash, LV_OBJ_FLAG_HIDDEN)) lv_obj_fade_out(skipFlash, 300, 0);
}

// The flash stays up (with a spinner) over the art, or the title dims under the controls,
// until the new track's metadata arrives -- the WiiM can take a second or two to switch.
void startSkip(bool next) {
  skipPending = true;
  skipStartedMs = lv_tick_get();
  if (controlsShown) {
    lv_obj_set_style_text_opa(titleLabel, LV_OPA_40, 0);
    lv_obj_set_style_text_opa(artistLabel, LV_OPA_40, 0);
    return;
  }
  lv_label_set_text(skipFlashIcon, next ? LV_SYMBOL_NEXT : LV_SYMBOL_PREV);
  lv_anim_del(skipFlash, NULL);
  lv_obj_set_style_opa(skipFlash, LV_OPA_COVER, 0);
  lv_obj_clear_flag(skipFlash, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(skipSpinner, LV_OBJ_FLAG_HIDDEN);
}

void skipTimerCb(lv_timer_t *) {
  if (skipPending && lv_tick_elaps(skipStartedMs) > kSkipWaitMaxMs) endSkip();
}

void onGesture(lv_event_t *e) {
  (void)e;
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) {
    return;
  }
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  // With only the art showing, swipes skip tracks; tap first to bring up the controls and page.
  if (activeScreen == kNowPlaying && !controlsShown && (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)) {
    bool next = dir == LV_DIR_LEFT;
    startSkip(next);
    if (next && callbacks.onNext) callbacks.onNext();
    if (!next && callbacks.onPrev) callbacks.onPrev();
    return;
  }
  if (dir == LV_DIR_LEFT) {
    goToScreen(activeScreen + 1, true);
  } else if (dir == LV_DIR_RIGHT) {
    goToScreen(activeScreen - 1, false);
  }
}

// One row of dots per page; layoutPageDots() places them, skipping disabled pages.
void makePageDots(lv_obj_t *parent, int page) {
  for (int i = 0; i < kScreenCount; i++) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, i == page ? lv_color_hex(kAccentColor) : lv_color_hex(0x555555), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    pageDots[page][i] = dot;
  }
}

void layoutPageDots() {
  constexpr int kSpacing = 14;
  int enabled = 0;
  for (int i = 0; i < kScreenCount; i++) enabled += pageEnabled[i];
  float mid = (enabled - 1) / 2.0f;
  for (int page = 0; page < kScreenCount; page++) {
    int slot = 0;
    for (int i = 0; i < kScreenCount; i++) {
      lv_obj_t *dot = pageDots[page][i];
      if (!dot) continue;
      if (!pageEnabled[i]) {
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, (int)((slot++ - mid) * kSpacing), -14);
    }
  }
}

// A small circular badge (volume level, battery gauge) -- not a playback button, so no
// gesture-bubble is added by the caller unless it asks for one.
lv_obj_t *makeBadge(lv_obj_t *parent, int size, int xOffset, int yOffset) {
  lv_obj_t *badge = lv_obj_create(parent);
  lv_obj_set_size(badge, size, size);
  lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(badge, 0, 0);
  lv_obj_set_style_bg_color(badge, lv_color_hex(kBadgeColor), 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_align(badge, LV_ALIGN_CENTER, xOffset, yOffset);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  return badge;
}

// A borderless hit target sitting on the transparent control bar -- not a visible round
// button, just an icon plus enough tappable area around it.
lv_obj_t *makeMediaButton(lv_obj_t *bar, int xOffset, int size, const char *symbol, lv_event_cb_t cb,
                          bool filled, lv_obj_t **iconOut = nullptr) {
  lv_obj_t *hit = lv_obj_create(bar);
  lv_obj_set_size(hit, size, size);
  lv_obj_set_style_radius(hit, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hit, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(hit, filled ? LV_OPA_90 : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(hit, filled ? LV_OPA_60 : LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(hit, 0, 0);
  lv_obj_set_style_pad_all(hit, 0, 0);
  lv_obj_align(hit, LV_ALIGN_CENTER, xOffset, 0);
  lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(hit, LV_OBJ_FLAG_GESTURE_BUBBLE); // a swipe starting here still changes screens
  lv_obj_add_event_cb(hit, cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *icon = lv_label_create(hit);
  lv_label_set_text(icon, symbol);
  lv_obj_set_style_text_font(icon, &lv_font_media_icons_36, 0);
  lv_obj_set_style_text_color(icon, filled ? lv_color_hex(0x111111) : lv_color_white(), 0);
  lv_obj_center(icon);
  if (iconOut) *iconOut = icon;
  return hit;
}

void setArtOn(lv_obj_t *img, const lv_img_dsc_t *art, int coverSize) {
  if (!art) {
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_img_set_src(img, art);
  // Cover the target size: zoom so the *shorter* side reaches it, cropping whatever overflows
  // the longer side (clipped by the circular panel itself, or by the inset's own clip_corner).
  int shortest = art->header.w < art->header.h ? art->header.w : art->header.h;
  if (shortest > 0) {
    lv_img_set_zoom(img, (256 * coverSize) / shortest);
  }
  lv_obj_center(img);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
}


void markLastFired(lv_obj_t *tile) {
  if (lastFiredTile) {
    lv_obj_set_style_border_color(lastFiredTile, lv_color_black(), 0);
    lv_obj_set_style_border_opa(lastFiredTile, LV_OPA_TRANSP, 0);
  }
  lastFiredTile = tile;
  if (tile) {
    lv_obj_set_style_border_color(tile, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_border_opa(tile, LV_OPA_COVER, 0);
  }
}

void onPresetClicked(lv_event_t *e) {
  // LVGL still sends CLICKED on release after a swipe that started on a tile -- don't let
  // swiping between screens fire a preset (which can switch inputs/outputs).
  lv_indev_t *indev = lv_indev_get_act();
  if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) {
    return;
  }
  lv_obj_t *tile = lv_event_get_current_target(e);
  int key = (int)(intptr_t)lv_event_get_user_data(e);
  lastFiredKey = key;
  markLastFired(tile);
  if (callbacks.onPreset) callbacks.onPreset(key);
}

void setRecentRing(lv_obj_t *tile, bool on) {
  lv_obj_set_style_outline_opa(tile, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void onRecentClicked(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_get_act();
  if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) {
    return; // the tail end of a swipe, not a tap -- see onPresetClicked
  }
  lv_obj_t *tile = lv_event_get_current_target(e);
  // Move the ring right away; the real "now playing" state catches up once the WiiM reports it.
  for (int i = 0; i < recentTileCount; i++) {
    setRecentRing(recentTiles[i], recentTiles[i] == tile);
  }
  if (callbacks.onRecent) callbacks.onRecent((int)(intptr_t)lv_event_get_user_data(e));
}

void onSettingClicked(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_get_act();
  if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) {
    return; // the tail end of a swipe, not a tap -- see onPresetClicked
  }
  if (callbacks.onSettingTapped) callbacks.onSettingTapped((int)(intptr_t)lv_event_get_user_data(e));
}

void onWifiSetupHeld(lv_event_t *) {
  if (callbacks.onWifiSetup) callbacks.onWifiSetup();
}

void onSetupCancelClicked(lv_event_t *) {
  if (setupCancel) setupCancel();
}

lv_obj_t *makePillButton(lv_obj_t *parent, const char *text, int y) {
  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_set_size(btn, 220, 46);
  lv_obj_set_style_radius(btn, 23, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(kAccentColor), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, y);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *label = lv_label_create(btn);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

const char *presetIconGlyph(PresetIcon icon) {
  switch (icon) {
  case PresetIcon::kDisc:
    return "\xEF\x94\x9F"; // U+F51F compact-disc
  case PresetIcon::kHeadphones:
    return "\xEF\x80\xA5"; // U+F025 headphones
  case PresetIcon::kEq:
    return "\xEF\x87\x9E"; // U+F1DE sliders-h
  case PresetIcon::kMusic:
  default:
    return "\xEF\x80\x81"; // U+F001 music
  }
}

lv_obj_t *makePresetTile(lv_obj_t *grid, const PresetTile &p) {
  lv_obj_t *tile = lv_obj_create(grid);
  lv_obj_set_size(tile, 120, 104);
  lv_obj_set_style_radius(tile, 16, 0);
  lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_10, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(tile, 2, 0);
  lv_obj_set_style_border_opa(tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(tile, 4, 0);
  lv_obj_set_style_pad_row(tile, 2, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(tile, onPresetClicked, LV_EVENT_CLICKED, (void *)(intptr_t)p.key);

  lv_obj_t *icon = lv_label_create(tile);
  lv_label_set_text(icon, presetIconGlyph(p.icon));
  lv_obj_set_style_text_font(icon, &lv_font_preset_icons_26, 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);

  lv_obj_t *name = lv_label_create(tile);
  lv_obj_set_width(name, 110);
  lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP); // "Headphone Mode" needs two lines
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(name, lv_color_white(), 0);
  lv_label_set_text(name, p.name);

  lv_obj_t *sub = lv_label_create(tile);
  lv_obj_set_width(sub, 110);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0xb4b2a9), 0);
  lv_label_set_text(sub, p.subtitle);

  if (p.key == lastFiredKey) markLastFired(tile);
  return tile;
}

} // namespace

void begin(const Callbacks &cbs) {
  callbacks = cbs;

  for (int i = 0; i < kScreenCount; i++) {
    screens[i] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screens[i], lv_color_black(), 0);
    lv_obj_clear_flag(screens[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screens[i], onGesture, LV_EVENT_GESTURE, NULL);
  }

  // ---- Now playing ----
  lv_obj_t *np = screens[kNowPlaying];
  artImg = lv_img_create(np);
  lv_obj_center(artImg);
  lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);
  // The art isn't clickable, so taps anywhere land on the screen itself.
  lv_obj_add_event_cb(np, onArtTapped, LV_EVENT_SHORT_CLICKED, NULL);

  controlsLayer = lv_obj_create(np);
  lv_obj_set_size(controlsLayer, 360, 360);
  lv_obj_center(controlsLayer);
  lv_obj_set_style_bg_opa(controlsLayer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(controlsLayer, 0, 0);
  lv_obj_set_style_pad_all(controlsLayer, 0, 0);
  lv_obj_clear_flag(controlsLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(controlsLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *ctrl = controlsLayer;

  frontImg = lv_img_create(np);
  lv_obj_center(frontImg);
  lv_obj_add_flag(frontImg, LV_OBJ_FLAG_HIDDEN);

  progressArc = lv_arc_create(np);
  lv_obj_set_size(progressArc, 356, 356);
  lv_obj_center(progressArc);
  lv_arc_set_bg_angles(progressArc, 0, 360);
  lv_arc_set_rotation(progressArc, 270);
  lv_arc_set_range(progressArc, 0, 1000);
  lv_obj_remove_style(progressArc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(progressArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(progressArc, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(progressArc, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(progressArc, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(progressArc, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_arc_color(progressArc, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(progressArc, LV_OPA_80, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(progressArc, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(progressArc, LV_OBJ_FLAG_HIDDEN);
  lv_timer_create(progressTimerCb, 1000, NULL);

  skipFlash = lv_obj_create(np);
  lv_obj_set_size(skipFlash, 96, 96);
  lv_obj_set_style_radius(skipFlash, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(skipFlash, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(skipFlash, LV_OPA_60, 0);
  lv_obj_set_style_border_width(skipFlash, 0, 0);
  lv_obj_center(skipFlash);
  lv_obj_clear_flag(skipFlash, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(skipFlash, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(skipFlash, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_pad_all(skipFlash, 0, 0);
  skipFlashIcon = lv_label_create(skipFlash);
  lv_obj_set_style_text_font(skipFlashIcon, &lv_font_media_icons_36, 0);
  lv_obj_set_style_text_color(skipFlashIcon, lv_color_white(), 0);
  lv_obj_center(skipFlashIcon);
  skipSpinner = lv_spinner_create(skipFlash, 900, 70);
  lv_obj_set_size(skipSpinner, 90, 90);
  lv_obj_center(skipSpinner);
  lv_obj_set_style_arc_width(skipSpinner, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(skipSpinner, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(skipSpinner, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_color(skipSpinner, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_clear_flag(skipSpinner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(skipSpinner, LV_OBJ_FLAG_HIDDEN);
  lv_timer_create(skipTimerCb, 250, NULL);

  tickerPill = lv_obj_create(np);
  lv_obj_set_size(tickerPill, 240, 34);
  lv_obj_set_style_radius(tickerPill, 17, 0);
  lv_obj_set_style_bg_color(tickerPill, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(tickerPill, LV_OPA_50, 0);
  lv_obj_set_style_border_width(tickerPill, 0, 0);
  lv_obj_set_style_pad_hor(tickerPill, 14, 0);
  lv_obj_set_style_pad_ver(tickerPill, 0, 0);
  lv_obj_align(tickerPill, LV_ALIGN_CENTER, 0, 128);
  lv_obj_clear_flag(tickerPill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(tickerPill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(tickerPill, LV_OBJ_FLAG_HIDDEN);

  tickerLabel = lv_label_create(tickerPill);
  lv_obj_set_width(tickerLabel, lv_pct(100));
  lv_label_set_long_mode(tickerLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(tickerLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(tickerLabel, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(tickerLabel, lv_color_white(), 0);
  lv_obj_center(tickerLabel);
  lv_label_set_text(tickerLabel, "");

  volumeArc = lv_arc_create(ctrl);
  lv_obj_set_size(volumeArc, 356, 356);
  lv_obj_center(volumeArc);
  lv_arc_set_bg_angles(volumeArc, 0, 360);
  lv_arc_set_rotation(volumeArc, 270);
  lv_arc_set_range(volumeArc, 0, 100);
  lv_arc_set_value(volumeArc, 0);
  lv_obj_remove_style(volumeArc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(volumeArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(volumeArc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(volumeArc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(volumeArc, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(volumeArc, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_arc_color(volumeArc, lv_color_hex(kAccentColor), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(volumeArc, LV_OPA_TRANSP, 0);

  volumeLabel = lv_label_create(ctrl);
  lv_obj_set_style_text_font(volumeLabel, &lv_font_montserrat_96_digits, 0);
  lv_obj_set_style_text_color(volumeLabel, lv_color_white(), 0);
  lv_obj_align(volumeLabel, LV_ALIGN_CENTER, 0, -50);
  lv_label_set_text(volumeLabel, "");

  batteryBadge = makeBadge(ctrl, 44, 0, -122);
  lv_obj_set_style_bg_opa(batteryBadge, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(batteryBadge, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(batteryBadge, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(batteryBadge, onBatteryBadgeClicked, LV_EVENT_CLICKED, NULL);

  batteryIconOutline = lv_obj_create(batteryBadge);
  lv_obj_set_size(batteryIconOutline, kBattIconW, kBattIconH);
  lv_obj_set_style_radius(batteryIconOutline, 3, 0);
  lv_obj_set_style_bg_opa(batteryIconOutline, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(batteryIconOutline, 2, 0);
  lv_obj_set_style_border_color(batteryIconOutline, lv_color_hex(0xcccccc), 0);
  // The default theme puts several px of padding on every plain lv_obj_create() container,
  // which shrinks the content area lv_obj_align() positions children within. At this icon's
  // tiny scale that was clipping most of the fill bar down to a sliver -- zero it out so our
  // own inset (the fill's own x offset below) is the only one in effect.
  lv_obj_set_style_pad_all(batteryIconOutline, 0, 0);
  lv_obj_center(batteryIconOutline);
  lv_obj_clear_flag(batteryIconOutline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(batteryIconOutline, LV_OBJ_FLAG_CLICKABLE);

  batteryIconNub = lv_obj_create(batteryBadge);
  lv_obj_set_size(batteryIconNub, 3, 8);
  lv_obj_set_style_radius(batteryIconNub, 1, 0);
  lv_obj_set_style_bg_color(batteryIconNub, lv_color_hex(0xcccccc), 0);
  lv_obj_set_style_bg_opa(batteryIconNub, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(batteryIconNub, 0, 0);
  lv_obj_align_to(batteryIconNub, batteryIconOutline, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
  lv_obj_clear_flag(batteryIconNub, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(batteryIconNub, LV_OBJ_FLAG_CLICKABLE);

  batteryFill = lv_obj_create(batteryIconOutline);
  lv_obj_set_size(batteryFill, kBattFillMaxW, kBattIconH - 6);
  lv_obj_set_style_radius(batteryFill, 1, 0);
  lv_obj_set_style_border_width(batteryFill, 0, 0);
  lv_obj_set_style_bg_opa(batteryFill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(batteryFill, lv_color_hex(kBatteryGreen), 0);
  lv_obj_align(batteryFill, LV_ALIGN_LEFT_MID, 3, 0);
  lv_obj_clear_flag(batteryFill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(batteryFill, LV_OBJ_FLAG_CLICKABLE);

  // A child of the outline, so it hides along with it when the badge flips to a percentage.
  batteryBolt = lv_label_create(batteryIconOutline);
  lv_obj_set_style_text_font(batteryBolt, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(batteryBolt, lv_color_white(), 0);
  lv_label_set_text(batteryBolt, LV_SYMBOL_CHARGE);
  lv_obj_center(batteryBolt);
  lv_obj_add_flag(batteryBolt, LV_OBJ_FLAG_HIDDEN);

  batteryPercentLabel = lv_label_create(batteryBadge);
  lv_obj_set_style_text_font(batteryPercentLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(batteryPercentLabel, lv_color_white(), 0);
  lv_obj_center(batteryPercentLabel);
  lv_label_set_text(batteryPercentLabel, "--%");
  lv_obj_add_flag(batteryPercentLabel, LV_OBJ_FLAG_HIDDEN); // shown only after a tap

  // Small connectivity dot at the top: green once the last WiiM poll succeeded, red if not.
  statusDot = lv_obj_create(ctrl);
  lv_obj_set_size(statusDot, 8, 8);
  lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(statusDot, 0, 0);
  lv_obj_set_style_bg_color(statusDot, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(statusDot, LV_ALIGN_CENTER, 0, -153);
  lv_obj_clear_flag(statusDot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(statusDot, LV_OBJ_FLAG_CLICKABLE);

  titleLabel = lv_label_create(ctrl);
  lv_obj_set_width(titleLabel, 260);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20_latin, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
  lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, 22);
  lv_label_set_text(titleLabel, "Not playing");

  artistLabel = lv_label_create(ctrl);
  lv_obj_set_width(artistLabel, 260);
  lv_label_set_long_mode(artistLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(artistLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(artistLabel, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(artistLabel, lv_color_hex(0xc8c8c8), 0);
  lv_obj_align(artistLabel, LV_ALIGN_CENTER, 0, 46);
  lv_label_set_text(artistLabel, "");

  // Under the media buttons, where the circle is only ~160px wide.
  albumLabel = lv_label_create(ctrl);
  lv_obj_set_width(albumLabel, 150);
  lv_label_set_long_mode(albumLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(albumLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(albumLabel, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(albumLabel, lv_color_hex(0x9a9a9a), 0);
  lv_obj_align(albumLabel, LV_ALIGN_CENTER, 0, 152);
  lv_label_set_text(albumLabel, "");

  lv_obj_t *controlBar = lv_obj_create(ctrl);
  lv_obj_set_size(controlBar, 250, 76);
  lv_obj_set_style_bg_opa(controlBar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(controlBar, 0, 0);
  lv_obj_set_style_pad_all(controlBar, 0, 0);
  lv_obj_align(controlBar, LV_ALIGN_CENTER, 0, 100);
  lv_obj_clear_flag(controlBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(controlBar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(controlBar, LV_OBJ_FLAG_GESTURE_BUBBLE);

  makeMediaButton(controlBar, -88, 64, LV_SYMBOL_PREV, onPrevClicked, false);
  makeMediaButton(controlBar, 0, 72, LV_SYMBOL_PLAY, onPlayPauseClicked, true, &playPauseIcon);
  makeMediaButton(controlBar, 88, 64, LV_SYMBOL_NEXT, onNextClicked, false);

  makePageDots(ctrl, kNowPlaying);
  lv_timer_create(controlsTimerCb, 250, NULL);

  // ---- Recently played ----
  lv_obj_t *rec = screens[kRecent];

  lv_obj_t *recentTitle = lv_label_create(rec);
  lv_obj_set_style_text_font(recentTitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(recentTitle, lv_color_hex(0xb4b2a9), 0);
  lv_label_set_text(recentTitle, "Recently played");
  lv_obj_align(recentTitle, LV_ALIGN_TOP_MID, 0, 46);

  lv_obj_t *recentHint = lv_label_create(rec);
  lv_obj_set_style_text_font(recentHint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(recentHint, lv_color_hex(0x707070), 0);
  lv_label_set_text(recentHint, "Tap to play");
  lv_obj_align(recentHint, LV_ALIGN_BOTTOM_MID, 0, -50);

  recentEmptyLabel = lv_label_create(rec);
  lv_obj_set_width(recentEmptyLabel, 220);
  lv_label_set_long_mode(recentEmptyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(recentEmptyLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(recentEmptyLabel, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(recentEmptyLabel, lv_color_hex(0xa0a0a0), 0);
  lv_label_set_text(recentEmptyLabel, "Albums you play will show up here");
  lv_obj_center(recentEmptyLabel);

  makePageDots(rec, kRecent);

  // ---- Presets ----
  lv_obj_t *pres = screens[kPresets];

  lv_obj_t *presetsTitle = lv_label_create(pres);
  lv_obj_set_style_text_font(presetsTitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(presetsTitle, lv_color_hex(0xb4b2a9), 0);
  lv_label_set_text(presetsTitle, "Presets");
  lv_obj_align(presetsTitle, LV_ALIGN_TOP_MID, 0, 34);

  // Two columns of tiles; scrolls vertically if more than four presets are set up. Only
  // vertical scrolling, so a horizontal swipe still reaches the screen's gesture handler.
  presetGrid = lv_obj_create(pres);
  lv_obj_set_size(presetGrid, 254, 222);
  lv_obj_align(presetGrid, LV_ALIGN_TOP_MID, 0, 62);
  lv_obj_set_style_bg_opa(presetGrid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(presetGrid, 0, 0);
  lv_obj_set_style_pad_all(presetGrid, 2, 0);
  lv_obj_set_style_pad_gap(presetGrid, 10, 0);
  lv_obj_set_flex_flow(presetGrid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(presetGrid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(presetGrid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(presetGrid, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(presetGrid, LV_OBJ_FLAG_GESTURE_BUBBLE);

  presetEmptyLabel = lv_label_create(pres);
  lv_obj_set_width(presetEmptyLabel, 220);
  lv_label_set_long_mode(presetEmptyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(presetEmptyLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(presetEmptyLabel, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(presetEmptyLabel, lv_color_hex(0xa0a0a0), 0);
  lv_label_set_text(presetEmptyLabel, "Loading presets...");
  lv_obj_center(presetEmptyLabel);

  makePageDots(pres, kPresets);

  // ---- Settings ----
  lv_obj_t *set = screens[kSettings];

  lv_obj_t *settingsTitle = lv_label_create(set);
  lv_obj_set_style_text_font(settingsTitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(settingsTitle, lv_color_hex(0xb4b2a9), 0);
  lv_label_set_text(settingsTitle, "Settings");
  lv_obj_align(settingsTitle, LV_ALIGN_TOP_MID, 0, 46);

  lv_obj_t *settingsHint = lv_label_create(set);
  lv_obj_set_style_text_font(settingsHint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(settingsHint, lv_color_hex(0x707070), 0);
  lv_label_set_text(settingsHint, "Tap a row, turn to adjust");
  lv_obj_align(settingsHint, LV_ALIGN_BOTTOM_MID, 0, -32);

  makePageDots(set, kSettings);

  lv_obj_t *net = screens[kNetwork];
  lv_obj_t *netTitle = lv_label_create(net);
  lv_obj_set_style_text_font(netTitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(netTitle, lv_color_hex(0xb4b2a9), 0);
  lv_label_set_text(netTitle, "Network");
  lv_obj_align(netTitle, LV_ALIGN_TOP_MID, 0, 46);

  static const char *const kNetLabels[] = {"Wi-Fi", "IP", "Signal", "WiiM"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *name = lv_label_create(net);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16_latin, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xa0a0a0), 0);
    lv_label_set_text(name, kNetLabels[i]);
    lv_obj_align(name, LV_ALIGN_CENTER, -80, -86 + i * 36);

    netValues[i] = lv_label_create(net);
    lv_obj_set_width(netValues[i], 170);
    lv_label_set_long_mode(netValues[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(netValues[i], &lv_font_montserrat_16_latin, 0);
    lv_obj_set_style_text_color(netValues[i], lv_color_white(), 0);
    lv_label_set_text(netValues[i], "--");
    lv_obj_align(netValues[i], LV_ALIGN_CENTER, 55, -86 + i * 36);
  }

  lv_obj_t *setupBtn = makePillButton(net, "Hold to set up Wi-Fi", 76);
  lv_obj_add_flag(setupBtn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(setupBtn, onWifiSetupHeld, LV_EVENT_LONG_PRESSED, NULL);

  makePageDots(net, kNetwork);

  layoutPageDots();
  lv_scr_load(screens[kNowPlaying]);
}

void setRecent(const RecentTile *tiles, int count) {
  // Rows of 3-4-3 centered on the circle; offsets from the screen center.
  static const int8_t kPos[kMaxRecentTiles][2] = {
      {-78, -72}, {0, -72}, {78, -72},
      {-117, 0}, {-39, 0}, {39, 0}, {117, 0},
      {-78, 72}, {0, 72}, {78, 72},
  };
  if (count > kMaxRecentTiles) count = kMaxRecentTiles;

  for (int i = 0; i < recentTileCount; i++) {
    lv_obj_del(recentTiles[i]);
    recentTiles[i] = nullptr;
  }
  recentTileCount = count;

  for (int i = 0; i < count; i++) {
    lv_obj_t *tile = lv_obj_create(screens[kRecent]);
    lv_obj_set_size(tile, kRecentThumbSize, kRecentThumbSize);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_clip_corner(tile, true, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_outline_width(tile, 2, 0);
    lv_obj_set_style_outline_pad(tile, 2, 0);
    lv_obj_set_style_outline_color(tile, lv_color_hex(kAccentColor), 0);
    setRecentRing(tile, tiles[i].playing);
    lv_obj_align(tile, LV_ALIGN_CENTER, kPos[i][0], kPos[i][1]);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(tile, onRecentClicked, LV_EVENT_CLICKED, (void *)(intptr_t)tiles[i].id);

    if (tiles[i].thumb) {
      lv_obj_t *img = lv_img_create(tile);
      lv_img_set_src(img, tiles[i].thumb);
      lv_obj_center(img);
    }
    recentTiles[i] = tile;
  }

  if (count == 0) {
    lv_obj_clear_flag(recentEmptyLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(recentEmptyLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void setPresets(const PresetTile *tiles, int count) {
  lastFiredTile = nullptr; // about to be deleted with the rest of the grid's children
  lv_obj_clean(presetGrid);
  for (int i = 0; i < count; i++) {
    makePresetTile(presetGrid, tiles[i]);
  }
  // Only scrollable when the tiles don't all fit. A scrollable container starts scrolling on
  // ~10px of finger drift even with nothing to scroll to, and a scroll swallows the tap --
  // which made taps on the touchscreen hit-or-miss.
  if (count > 4) {
    lv_obj_add_flag(presetGrid, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(presetGrid, LV_OBJ_FLAG_SCROLLABLE);
  }
  if (count == 0) {
    lv_label_set_text(presetEmptyLabel, "No presets set up in the WiiM Home app");
    lv_obj_clear_flag(presetEmptyLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(presetEmptyLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void setSettings(const char *const *labels, int count) {
  if (count > kMaxSettings) count = kMaxSettings;
  settingCount = count;
  constexpr int kRowH = 40, kGap = 5;
  int top = -((count * kRowH + (count - 1) * kGap) / 2) + kRowH / 2 - 6;
  for (int i = 0; i < count; i++) {
    lv_obj_t *row = lv_obj_create(screens[kSettings]);
    lv_obj_set_size(row, 256, kRowH);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, top + i * (kRowH + kGap));
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(row, onSettingClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16_latin, 0);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    lv_label_set_text(name, labels[i]);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16_latin, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(0xc8c8c8), 0);
    lv_label_set_text(val, "");
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

    settingRows[i] = row;
    settingValues[i] = val;
  }
}

void setSettingValue(int index, const char *text) {
  if (index < 0 || index >= settingCount) return;
  lv_label_set_text(settingValues[index], text);
}

void setSelectedSetting(int index) {
  for (int i = 0; i < settingCount; i++) {
    lv_obj_set_style_border_opa(settingRows[i], i == index ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
}

bool isSettingsActive() { return activeScreen == kSettings; }

void setRecentEnabled(bool enabled) {
  if (pageEnabled[kRecent] == enabled) return;
  pageEnabled[kRecent] = enabled;
  layoutPageDots();
}

void setNetworkInfo(const char *ssid, const char *ip, const char *signal, const char *wiim) {
  const char *values[] = {ssid, ip, signal, wiim};
  for (int i = 0; i < 4; i++) {
    if (strcmp(lv_label_get_text(netValues[i]), values[i]) != 0) lv_label_set_text(netValues[i], values[i]);
  }
}

void showSetup(const char *apName, const char *qrPayload, void (*onCancel)()) {
  setupCancel = onCancel;
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20_latin, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "Wi-Fi setup");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -138);

  lv_obj_t *qr = lv_qrcode_create(scr, 120, lv_color_black(), lv_color_white());
  lv_qrcode_update(qr, qrPayload, strlen(qrPayload));
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 6, 0);
  lv_obj_align(qr, LV_ALIGN_CENTER, 0, -54);

  lv_obj_t *hint = lv_label_create(scr);
  lv_obj_set_width(hint, 250);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0xc8c8c8), 0);
  lv_label_set_text_fmt(hint, "Scan with your phone, or join\n%s", apName);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 36);

  setupStatus = lv_label_create(scr);
  lv_obj_set_width(setupStatus, 240);
  lv_label_set_long_mode(setupStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(setupStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(setupStatus, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(setupStatus, lv_color_white(), 0);
  lv_label_set_text(setupStatus, "");
  lv_obj_align(setupStatus, LV_ALIGN_CENTER, 0, 82);

  lv_obj_t *cancel = makePillButton(scr, "Cancel", 132);
  lv_obj_set_size(cancel, 140, 40);
  lv_obj_set_style_bg_color(cancel, lv_color_hex(0x2a2a2a), 0);
  lv_obj_add_event_cb(cancel, onSetupCancelClicked, LV_EVENT_CLICKED, NULL);

  lv_scr_load(scr);
}

void setSetupStatus(const char *text) {
  if (setupStatus) lv_label_set_text(setupStatus, text);
}

void setTrack(const char *title, const char *artist, const char *album) {
  endSkip();
  lv_label_set_text(titleLabel, (title && title[0]) ? title : "Not playing");
  lv_label_set_text(artistLabel, artist ? artist : "");
  lv_label_set_text(albumLabel, album ? album : "");

  if (title && title[0] && artist && artist[0]) {
    lv_label_set_text_fmt(tickerLabel, "%s \xE2\x80\xA2 %s", title, artist); // U+2022 bullet
  } else {
    lv_label_set_text(tickerLabel, (title && title[0]) ? title : "");
  }
  if (!lv_label_get_text(tickerLabel)[0]) lv_obj_add_flag(tickerPill, LV_OBJ_FLAG_HIDDEN);
}

void setAlbumArt(const lv_img_dsc_t *art, const lv_img_dsc_t *tinted) {
  plainArt = art;
  tintedArt = tinted;
  if (!art) setControlsShown(true);
  applyArt();
}

void setVolume(int volume0to100) {
  static int last = -1;
  if (last == volume0to100) return;
  last = volume0to100;
  lv_arc_set_value(volumeArc, volume0to100);
  lv_label_set_text_fmt(volumeLabel, "%d", volume0to100);
}

void hideControlsNow() {
  setControlsShown(false);
  if (controlsShown) return; // no art, so the controls stay
  lv_anim_del(frontImg, frontImgOpaCb);
  lv_obj_set_style_img_opa(frontImg, LV_OPA_COVER, 0);
  onControlsFadedOut(nullptr);
}

void setControlsTimeout(uint32_t seconds) { controlsTimeoutMs = seconds * 1000; }

void showControls() {
  lv_disp_trig_activity(NULL); // restarts the hide timer; knob turns and wakes aren't LVGL input
  setControlsShown(true);
}

// These get called every loop() pass with whatever the WiiM last reported. Re-applying an
// unchanged value still costs LVGL a text relayout or style refresh, so skip those.
void setPlaying(bool playing) {
  static int last = -1;
  if (last == (int)playing) return;
  last = playing;
  lv_label_set_text(playPauseIcon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

void setProgress(long positionMs, long durationMs, bool playing) {
  if (positionMs == progressBaseMs && durationMs == progressDurationMs && playing == progressPlaying) return;
  if (skipPending && positionMs < 3000 && progressBaseMs > 5000) endSkip(); // "previous" restarted the same track
  progressBaseMs = positionMs;
  progressDurationMs = durationMs;
  progressPlaying = playing;
  progressBaseTick = lv_tick_get();
  if (durationMs <= 0) {
    lv_obj_add_flag(progressArc, LV_OBJ_FLAG_HIDDEN);
  } else if (!controlsShown) {
    lv_obj_clear_flag(progressArc, LV_OBJ_FLAG_HIDDEN);
  }
}

void setConnected(bool connected) {
  static int last = -1;
  if (last == (int)connected) return;
  last = connected;
  lv_obj_set_style_bg_color(statusDot,
                             connected ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
}

void setBattery(int percent0to100, bool onUsbPower) {
  if (percent0to100 < 0) percent0to100 = 0;
  if (percent0to100 > 100) percent0to100 = 100;

  int fillWidth = (kBattFillMaxW * percent0to100) / 100;
  if (fillWidth < 2) fillWidth = 2; // keep a sliver visible even near-empty
  lv_obj_set_width(batteryFill, fillWidth);

  uint32_t color = kBatteryGreen;
  if (percent0to100 <= 15) {
    color = 0xE24B4A; // red
  } else if (percent0to100 <= 35) {
    color = 0xEF9F27; // amber
  }
  lv_obj_set_style_bg_color(batteryFill, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(batteryFill, onUsbPower ? LV_OPA_50 : LV_OPA_COVER, 0); // lets the bolt stand out

  if (onUsbPower) {
    lv_obj_clear_flag(batteryBolt, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(batteryBolt, LV_OBJ_FLAG_HIDDEN);
  }

  lv_label_set_text_fmt(batteryPercentLabel, "%d%%", percent0to100);
}

} // namespace ui
