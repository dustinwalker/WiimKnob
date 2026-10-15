#include "UiInternal.h"

// The Settings and Network pages, and the full-screen Wi-Fi setup screen.
namespace ui {

using namespace detail;

namespace {

// Settings.
constexpr int kMaxSettings = 8;
lv_obj_t *settingsList = nullptr;
lv_obj_t *settingRows[kMaxSettings] = {};
lv_obj_t *settingValues[kMaxSettings] = {};
int settingCount = 0;

// Network.
lv_obj_t *netValues[4] = {};

// Wi-Fi setup.
lv_obj_t *setupStatus = nullptr;
void (*setupCancel)() = nullptr;

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

} // namespace

void detail::buildSettingsScreens() {
  lv_obj_t *set = screens[kSettings];

  lv_obj_t *settingsTitle = lv_label_create(set);
  lv_obj_set_style_text_font(settingsTitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(settingsTitle, lv_color_hex(0xb4b2a9), 0);
  lv_label_set_text(settingsTitle, "Settings");
  lv_obj_align(settingsTitle, LV_ALIGN_TOP_MID, 0, 46);

  // Scrolls vertically only, so a horizontal swipe still reaches the screen and changes pages.
  settingsList = lv_obj_create(set);
  lv_obj_set_size(settingsList, 280, 236);
  lv_obj_align(settingsList, LV_ALIGN_CENTER, 0, 4);
  lv_obj_set_style_bg_opa(settingsList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(settingsList, 0, 0);
  lv_obj_set_style_pad_all(settingsList, 4, 0);
  lv_obj_set_style_pad_row(settingsList, 6, 0);
  lv_obj_set_flex_flow(settingsList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(settingsList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(settingsList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(settingsList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(settingsList, LV_OBJ_FLAG_GESTURE_BUBBLE);

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
}

void setSettings(const char *const *labels, int count) {
  if (count > kMaxSettings) count = kMaxSettings;
  settingCount = count;
  for (int i = 0; i < count; i++) {
    lv_obj_t *row = lv_obj_create(settingsList);
    lv_obj_set_size(row, 256, 46);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
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

void setNetworkInfo(const char *ssid, const char *ip, const char *signal, const char *wiim) {
  const char *values[] = {ssid, ip, signal, wiim};
  for (int i = 0; i < 4; i++) {
    if (strcmp(lv_label_get_text(netValues[i]), values[i]) != 0) lv_label_set_text(netValues[i], values[i]);
  }
}

void showSetup(const char *apName, const char *apPassword, const char *qrPayload, void (*onCancel)()) {
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
  lv_label_set_text_fmt(hint, "Scan with your phone, or join\n%s  /  %s", apName, apPassword);
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

void showMessage(const char *title, const char *body) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(scr);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20_latin, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(kAccentColor), 0);
  lv_label_set_text(t, title);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t *b = lv_label_create(scr);
  lv_obj_set_width(b, 240);
  lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(b, &lv_font_montserrat_16_latin, 0);
  lv_obj_set_style_text_color(b, lv_color_white(), 0);
  lv_label_set_text(b, body);
  lv_obj_align(b, LV_ALIGN_CENTER, 0, 20);

  lv_scr_load(scr);
}

} // namespace ui
