#pragma once

#include "Ui.h"

// Generated from Montserrat-Medium.ttf + the same FontAwesome symbol subset LVGL's own
// built-in fonts use, but with the main range extended to 0x20-0x7F,0xA0-0x17F,0x2022 instead
// of just ASCII -- see src/ui/fonts/ for the command. The stock lv_font_montserrat_16/20 only
// cover ASCII plus the icon symbols, so any accented character (e.g. the O with diaeresis in
// "Magnus Ostrom") had no glyph to draw at all. Only used where real track/artist text shows
// up; the icon buttons and the volume number stay on the stock fonts.
LV_FONT_DECLARE(lv_font_montserrat_20_latin);
LV_FONT_DECLARE(lv_font_montserrat_16_latin);

// State Ui.cpp shares with UiSettings.cpp.
namespace ui {
namespace detail {

// Red is reserved for "this is adjustable/live": the volume ring, plus the "last tapped"
// markers on the presets/recent screens. Everything else is neutral (dark badges, white icons)
// per the reference look.
constexpr uint32_t kAccentColor = 0xE53935;

// Swipe order, left to right. Boots on now playing.
constexpr int kNowPlaying = 0;
constexpr int kRecent = 1;
constexpr int kPresets = 2;
constexpr int kSettings = 3;
constexpr int kNetwork = 4;
constexpr int kScreenCount = 5;

extern Callbacks callbacks;
extern lv_obj_t *screens[kScreenCount];
extern int activeScreen;

void makePageDots(lv_obj_t *parent, int page);
void buildSettingsScreens(); // UiSettings.cpp; called from begin()

} // namespace detail
} // namespace ui
