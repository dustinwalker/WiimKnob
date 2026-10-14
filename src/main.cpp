#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>
#include <lvgl.h>

#include "Config.h"
#include "display/DisplayDriver.h"
#include "haptics/Haptics.h"
#include "input/Knob.h"
#include "input/Touch.h"
#include "net/NetConfig.h"
#include "net/SetupPortal.h"
#include "power/Battery.h"
#include "power/Power.h"
#include "settings/Settings.h"
#include "ui/Ui.h"
#include "wiim/WiimTask.h"


namespace {

constexpr uint32_t kBatteryPollMs = 5000;

input::Touch touch;

int localVolume = 0;
uint32_t lastKnobActivityMs = 0;
uint32_t lastSeenMetaVersion = 0;
uint32_t lastSeenPresetsVersion = 0;
uint32_t lastSeenRecentVersion = 0;
uint32_t lastRecentMetaVersion = 0;
uint32_t lastBatteryPollMs = 0;

void touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  (void)drv;
  static bool down = false;
  static bool swallowing = false; // a touch that started on a dimmed screen only undims it
  int16_t x, y;
  bool pressed = touch.read(x, y);
  if (pressed && !down) swallowing = power::isDimmed();
  down = pressed;
  if (pressed) power::noteActivity();
  if (pressed && !swallowing) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void onPrev() {
  wiim::task::requestPrevious();
  haptics::buzz();
}
void onNext() {
  wiim::task::requestNext();
  haptics::buzz();
}
void onPlayPause() {
  wiim::task::requestTogglePause();
  haptics::buzz();
}
void onPreset(int key) {
  wiim::task::requestPreset(key);
  haptics::buzz();
}
void onRecent(int slot) {
  wiim::task::requestRecent(slot);
  haptics::buzz();
}

int selectedSetting = settings::kBrightness;

void applySetting(settings::Item item) {
  int v = settings::value(item);
  switch (item) {
  case settings::kBrightness:
    display::setBrightness(v);
    break;
  case settings::kControlsTimeout:
    ui::setControlsTimeout(v);
    break;
  case settings::kScreenTimeout:
    power::setScreenTimeout(v);
    break;
  case settings::kSleepTimeout:
    power::setSleepTimeout(v);
    break;
  default:
    break;
  }
  char text[16];
  settings::format(item, text, sizeof(text));
  ui::setSettingValue(item, text);
}

void onSettingTapped(int index) {
  selectedSetting = index;
  ui::setSelectedSetting(index);
  haptics::buzz();
}

// Describes a preset by what its routine changes, since that's all the WiiM stores about it
// (no artwork): input first, then a USB DAC output, else just "EQ".
void describePreset(const wiim::Preset &p, bool wiimOnEthernet, String &subtitle, ui::PresetIcon &icon) {
  if (p.input == "wifi") {
    subtitle = wiimOnEthernet ? "ethernet" : "wi-fi"; // "wifi" here means network streaming
  } else {
    subtitle = p.input;
  }
  if (p.usbOutput) {
    // U+2022 bullet: one of the few non-ASCII glyphs the stock montserrat_14 includes. Kept
    // short -- "ethernet • USB DAC" doesn't fit a tile's width.
    subtitle += subtitle.length() ? " \xE2\x80\xA2 USB" : "USB DAC";
  }
  if (subtitle.length() == 0) {
    subtitle = p.changesEq ? "EQ" : "routine";
  }

  if (p.usbOutput) {
    icon = ui::PresetIcon::kHeadphones;
  } else if (p.input.length() && p.input != "wifi") {
    icon = ui::PresetIcon::kDisc; // a physical input: turntable, CD, ...
  } else if (p.input.length() == 0 && p.changesEq) {
    icon = ui::PresetIcon::kEq;
  } else {
    icon = ui::PresetIcon::kMusic;
  }
}

// Rebuilt when the saved list changes, and on every track change too so the red ring follows
// whichever album is actually playing.
void syncRecent() {
  uint32_t version = wiim::task::recentVersion();
  uint32_t metaVersion = wiim::task::metadataVersion();
  if (version == lastSeenRecentVersion && metaVersion == lastRecentMetaVersion) {
    return;
  }
  lastSeenRecentVersion = version;
  lastRecentMetaVersion = metaVersion;

  wiim::recent::List list = wiim::task::getRecent();
  wiim::TrackMetadata meta = wiim::task::getMetadata();
  ui::RecentTile tiles[wiim::recent::kMaxAlbums];
  for (int i = 0; i < list.count; i++) {
    const wiim::recent::Entry &e = list.items[i];
    tiles[i].id = e.slot;
    tiles[i].thumb = wiim::task::getRecentThumbnail(e.slot);
    // Title only: the WiiM reports each track's own artist, which needn't match the album's
    // (a "Thomas Bangalter" track on a Daft Punk album).
    tiles[i].playing = e.album.length() > 0 && e.album.equalsIgnoreCase(meta.album);
  }
  ui::setRecent(tiles, list.count);
}

void syncPresets() {
  uint32_t version = wiim::task::presetsVersion();
  if (version == lastSeenPresetsVersion) {
    return;
  }
  lastSeenPresetsVersion = version;

  wiim::PresetList list = wiim::task::getPresets();
  String subtitles[wiim::kMaxPresets];
  ui::PresetTile tiles[wiim::kMaxPresets];
  for (int i = 0; i < list.count; i++) {
    const wiim::Preset &p = list.items[i];
    tiles[i].key = p.key;
    tiles[i].name = p.name.c_str();
    describePreset(p, list.wiimOnEthernet, subtitles[i], tiles[i].icon);
    tiles[i].subtitle = subtitles[i].c_str();
  }
  ui::setPresets(tiles, list.count);
}

// Screen-off double tap: toggles play/pause without waking the screen. A single tap still
// wakes it, just after the double-tap window has passed (it has to wait to see whether a
// second tap is coming). LVGL isn't running while the screen is off, so this reads the touch
// controller directly and does its own press-edge detection.
constexpr uint32_t kScreenOffDoubleTapMs = 350;
bool screenOffTouchDown = false;
uint32_t screenOffPendingTapMs = 0; // 0 = no first tap waiting

void serviceScreenOffTouch() {
  int16_t x, y;
  bool down = touch.read(x, y);
  bool pressed = down && !screenOffTouchDown;
  screenOffTouchDown = down;
  uint32_t now = millis();

  if (pressed) {
    if (screenOffPendingTapMs != 0 && now - screenOffPendingTapMs <= kScreenOffDoubleTapMs) {
      screenOffPendingTapMs = 0;
      onPlayPause(); // buzzes, so there's feedback even with the screen dark
    } else {
      screenOffPendingTapMs = now ? now : 1;
    }
    return;
  }
  if (screenOffPendingTapMs != 0 && now - screenOffPendingTapMs > kScreenOffDoubleTapMs) {
    screenOffPendingTapMs = 0;
    power::noteActivity(); // just one tap: wake the screen as before
  }
}

// Over-the-air updates: `pio run -e esp32-s3-knob-ota -t upload` sends a new build to
// wiimknob.local over Wi-Fi. The knob has to be awake (not in deep sleep) to receive it.
void beginOta() {
  ArduinoOTA.setHostname("wiimknob");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.onStart([]() {
    // Modem sleep adds hundreds of ms of latency, enough to break the upload's TCP stream
    // partway through. The knob reboots afterwards, which restores the default.
    WiFi.setSleep(false);
    Serial.println("[ota] update starting");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    power::noteActivity(); // don't time out into deep sleep mid-update
  });
  ArduinoOTA.onEnd([]() { Serial.println("[ota] done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t err) {
    WiFi.setSleep(true);
    Serial.printf("[ota] error %u\n", err);
  });
  ArduinoOTA.begin();
}

// Read-only diagnostics at http://wiimknob.local/status, for checking on the knob without a
// USB cable (its serial log is only reachable over USB).
WebServer statusServer(80);
power::battery::Reading lastBattery;

void beginStatusServer() {
  statusServer.on("/status", HTTP_GET, []() {
    wiim::task::Diagnostics d = wiim::task::diagnostics();
    char body[448];
    snprintf(body, sizeof(body),
             "{\"voltage\":%.3f,\"percent\":%d,\"usb_power\":%s,\"uptime_s\":%lu,"
             "\"heap_internal\":%u,\"heap_internal_min\":%u,\"heap_internal_largest\":%u,"
             "\"rssi\":%d,\"meta_ok\":%u,\"meta_failures\":%u,\"art_failures\":%u,"
             "\"meta_age_s\":%lu}\n",
             lastBattery.voltage, lastBattery.percent, lastBattery.externalPower ? "true" : "false",
             millis() / 1000, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), WiFi.RSSI(), d.metaOk,
             d.metaFailures, d.artFailures, (millis() - d.lastMetaOkMs) / 1000);
    statusServer.send(200, "application/json", body);
  });
  statusServer.begin();
}

net::Config netConfig;

bool connectWiFi(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(netConfig.ssid.c_str(), netConfig.password.c_str());
  Serial.printf("Connecting to Wi-Fi '%s'...\n", netConfig.ssid.c_str());
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - started > timeoutMs) {
      Serial.println("\nCouldn't connect");
      return false;
    }
    display::tick();
    delay(50);
  }
  Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

void onWifiSetup() {
  haptics::buzz();
  net::requestSetup();
  ESP.restart();
}

void setupTick() { display::tick(); }

const char *signalText(int rssi, char *buf, size_t len) {
  const char *quality = rssi > -60 ? "Excellent" : rssi > -67 ? "Good" : rssi > -75 ? "Fair" : "Weak";
  snprintf(buf, len, "%s (%d dBm)", quality, rssi);
  return buf;
}

void serviceNetworkInfo() {
  static uint32_t lastMs = 0;
  if (millis() - lastMs < 2000) return;
  lastMs = millis();
  char signal[32];
  String wiim = netConfig.wiimName.length() ? netConfig.wiimName : netConfig.wiimHost;
  ui::setNetworkInfo(netConfig.ssid.c_str(), WiFi.localIP().toString().c_str(),
                     signalText(WiFi.RSSI(), signal, sizeof(signal)), wiim.c_str());
}

// Mirrors whatever the background WiiM task last found onto the UI. Cheap (a couple of mutex-
// protected struct copies), so it's fine to call every loop() iteration -- no polling timer of
// its own needed here, since wiim::task owns that cadence.
void syncUiFromWiimTask() {
  ui::setConnected(wiim::task::isConnected());

  wiim::PlayerState state = wiim::task::getPlayerState();
  ui::setPlaying(state.status == "play");
  ui::setProgress(state.positionMs, state.durationMs, state.status == "play");

  // Don't fight the knob: if the user just turned it, our optimistic local value is more
  // current than whatever the device reports until it catches up.
  if (millis() - lastKnobActivityMs > 1500) {
    localVolume = state.volume;
    ui::setVolume(localVolume);
  }

  uint32_t version = wiim::task::metadataVersion();
  if (version != lastSeenMetaVersion) {
    lastSeenMetaVersion = version;
    wiim::TrackMetadata meta = wiim::task::getMetadata();
    ui::setTrack(meta.title.c_str(), meta.artist.c_str(), meta.album.c_str());
    ui::setAlbumArt(wiim::task::getAlbumArt(), wiim::task::getAlbumArtTinted());
  }
}

void serviceKnob() {
  int delta = input::knob::consumeDelta();
  if (delta == 0) {
    return;
  }
  bool screenWasOn = power::isScreenOn(); // a blind turn in the dark stays volume, not settings
  power::noteActivity();
  if (screenWasOn && ui::isSettingsActive()) {
    if (settings::step((settings::Item)selectedSetting, delta)) {
      applySetting((settings::Item)selectedSetting);
    }
    display::refreshNow();
    return;
  }
  lastKnobActivityMs = millis();
  localVolume = constrain(localVolume + delta * VOLUME_STEP_PER_DETENT, 0, 100);
  ui::setVolume(localVolume);
  ui::showControls();
  wiim::task::requestVolume(localVolume);
  if (power::isScreenOn()) {
    display::refreshNow(); // show this detent now, not on the next refresh tick
  }
}

// On USB power, trade battery-saving for responsiveness: stay awake, keep the radio out of
// modem sleep (which adds 100-400ms of latency to every WiiM request), and poll faster.
bool onExternalPower = false;
constexpr uint32_t kExternalStatusPollMs = 500;

void applyPowerSource(bool external) {
  onExternalPower = external;
  power::setExternalPower(external);
  WiFi.setSleep(!external);
  wiim::task::setStatusPollMs(external ? kExternalStatusPollMs : POLL_STATUS_MS);
  Serial.printf("[power] %s\n", external ? "USB power: staying awake" : "on battery");
}

void serviceBattery() {
  uint32_t now = millis();
  if (now - lastBatteryPollMs < kBatteryPollMs) {
    return;
  }
  lastBatteryPollMs = now;
  power::battery::Reading reading = power::battery::read();
  lastBattery = reading;
  ui::setBattery(reading.percent, reading.externalPower);
  if (reading.externalPower != onExternalPower) applyPowerSource(reading.externalPower);

  // Not shown on screen anymore (see the battery screen removal) -- kept here so the raw
  // voltage is still checkable for calibrating BATTERY_DIVIDER_RATIO in Config.h.
  Serial.printf("[battery] %.2fV -> %d%%, ~%.1fh left%s\n", reading.voltage, reading.percent,
                reading.hoursRemaining, reading.externalPower ? " (USB power)" : "");
}

} // namespace

void setup() {
  Serial.begin(115200);

  display::begin();

  ui::Callbacks callbacks = {onPrev, onNext, onPlayPause, onPreset, onRecent, onSettingTapped, onWifiSetup};
  ui::begin(callbacks);

  settings::begin();
  const char *settingLabels[settings::kCount];
  for (int i = 0; i < settings::kCount; i++) settingLabels[i] = settings::label((settings::Item)i);
  ui::setSettings(settingLabels, settings::kCount);
  ui::setSelectedSetting(selectedSetting);
  for (int i = 0; i < settings::kCount; i++) applySetting((settings::Item)i);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchReadCb;
  lv_indev_drv_register(&indevDrv);

  touch.begin();
  haptics::begin(); // after touch.begin(), which brings up the I2C bus haptics shares
  input::knob::begin();
  power::begin();
  power::battery::begin();

  netConfig = net::load();
  bool setupRequested = net::takeSetupRequest();
  if (setupRequested || !connectWiFi(20000)) {
    net::portal::run(netConfig, setupTick);
  }
  beginOta();
  beginStatusServer();

  wiim::task::begin(netConfig.wiimHost);
}

void loop() {
  static bool screenWasOn = true;
  bool screenOn = power::isScreenOn();
  if (!screenOn && screenWasOn) {
    ui::hideControlsNow(); // a touch wakes to the art; a knob turn brings the controls itself
  }
  screenWasOn = screenOn;
  if (power::isScreenOn()) {
    display::tick(); // pumps LVGL, which reads touch via the indev callback above
    screenOffTouchDown = true; // a finger still down as the screen times out isn't a new tap
    screenOffPendingTapMs = 0;
  } else {
    // LVGL isn't running, so nothing is calling touchReadCb -- poll directly for a wake tap or
    // a double-tap. The next loop() iteration resumes display::tick() as normal once
    // noteActivity() turns the backlight back on.
    serviceScreenOffTouch();
  }
  serviceKnob();
  syncUiFromWiimTask();
  syncPresets();
  syncRecent();
  serviceBattery();
  power::service();
  settings::service();
  serviceNetworkInfo();
  ArduinoOTA.handle();
  statusServer.handleClient();

  delay(5);
}
