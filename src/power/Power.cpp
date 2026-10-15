#include "Power.h"
#include "Config.h"
#include "Pins.h"
#include "Battery.h"
#include "display/DisplayDriver.h"

#include <Arduino.h>
#include <Preferences.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <sys/time.h>

namespace power {

namespace {

uint32_t lastActivityMs = 0;
bool screenOn = true;
uint32_t screenTimeoutMs = (uint32_t)SCREEN_TIMEOUT_S * 1000;
uint32_t sleepTimeoutMs = (uint32_t)DEEP_SLEEP_TIMEOUT_MIN * 60000UL;
bool externalPower = false;
bool dimmed = false;
void (*screenCallback)(bool on) = nullptr;
void (*beforeSleep)() = nullptr;
constexpr uint32_t kDimLeadMs = 15000; // how long before screen-off the backlight dims

// Survive deep sleep (RTC slow memory), but not a power loss: after a flat battery they read 0,
// which is itself the answer to "did it run flat or fail to wake?".
RTC_DATA_ATTR float rtcSleepVolts = 0;
RTC_DATA_ATTR int64_t rtcSleepAtS = 0;
RTC_DATA_ATTR char rtcSleepReason[24] = "";
BootInfo bootInfo;
esp_reset_reason_t bootReset = ESP_RST_UNKNOWN;
uint8_t bootWake = 0; // 0 none, 1 knob, 2 touch, 3 timer

// The last few boots, in NVS so they survive a flat battery (the RTC fields above don't).
struct BootLogEntry {
  uint32_t epoch;    // wall clock (once NTP has set it), else seconds since power-on
  int32_t sleptS;    // -1 if this boot didn't follow a recorded deep sleep
  uint16_t sleepMv;
  uint16_t bootMv;
  uint8_t reset;     // esp_reset_reason_t
  uint8_t wake;
  uint8_t sleepWhy;  // 0 none/other, 1 idle timeout, 2 battery low, 3 no Wi-Fi
  uint8_t unused;
};
constexpr int kBootLogSize = 16;

int64_t nowSeconds() {
  struct timeval tv;
  gettimeofday(&tv, nullptr); // kept running through deep sleep by the RTC
  return tv.tv_sec;
}

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON: return "power-on";
  case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
  case ESP_RST_SW: return "software restart";
  case ESP_RST_PANIC: return "crash";
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT: return "watchdog";
  case ESP_RST_BROWNOUT: return "brownout";
  default: return "other";
  }
}

void enterDeepSleep(const char *reason, uint32_t wakeAfterS = 0) {
  display::setBacklightOn(false);
  if (wakeAfterS) esp_sleep_enable_timer_wakeup((uint64_t)wakeAfterS * 1000000ULL);

  // Wake on the same GPIOs the knob and touch controller already pulse low during normal
  // operation (see Pins.h) -- no extra wiring or wake button needed. ANY_LOW: wake as soon as
  // any one of them goes low, which is what a knob detent or a touch-down event does.
  //
  // esp_sleep_enable_ext1_wakeup() only sets the wake *condition*; it doesn't configure the
  // pins themselves. Without an explicit pull-up, ESP32-S3's RTC IO can leave a wake pin
  // floating once the regular digital GPIO drivers power down for sleep, and a floating pin
  // reading low satisfies ANY_LOW immediately -- waking the chip back up within microseconds,
  // which looks exactly like the sleep never happened.
  gpio_num_t wakeGpios[] = {(gpio_num_t)PIN_KNOB_CW, (gpio_num_t)PIN_KNOB_CCW, (gpio_num_t)PIN_TOUCH_INT};
  for (gpio_num_t pin : wakeGpios) {
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
  }

  uint64_t wakePins = (1ULL << PIN_KNOB_CW) | (1ULL << PIN_KNOB_CCW) | (1ULL << PIN_TOUCH_INT);
  esp_sleep_enable_ext1_wakeup(wakePins, ESP_EXT1_WAKEUP_ANY_LOW);

  if (beforeSleep) beforeSleep();
  rtcSleepVolts = battery::lastRawVoltage();
  rtcSleepAtS = nowSeconds();
  strlcpy(rtcSleepReason, reason, sizeof(rtcSleepReason));

  Serial.printf("%s, entering deep sleep. Turn the knob or touch the screen to wake.\n", reason);
  Serial.flush();
  display::sleepPanel();

  // ext1's wake condition is level-based, checked continuously once asleep. A brief settle
  // delay here is cheap insurance against a wake pin still transitioning right as we fall
  // asleep, which would otherwise wake the chip back up within microseconds.
  delay(500);

  esp_deep_sleep_start(); // does not return; the board reboots into setup() on wake
}

} // namespace

void begin() {
  lastActivityMs = millis();
  esp_reset_reason_t reset = esp_reset_reason();
  bootInfo.resetReason = resetReasonName(reset);
  bootReset = reset;
  bootInfo.wakeSource = "none";
  if (reset == ESP_RST_DEEPSLEEP && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t pins = esp_sleep_get_ext1_wakeup_status();
    bootInfo.wakeSource = (pins & (1ULL << PIN_TOUCH_INT)) ? "touch" : "knob";
    bootWake = (pins & (1ULL << PIN_TOUCH_INT)) ? 2 : 1;
  } else if (reset == ESP_RST_DEEPSLEEP && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    bootInfo.wakeSource = "timer";
    bootWake = 3;
  }
  if (rtcSleepAtS > 0) {
    bootInfo.sleptSeconds = nowSeconds() - rtcSleepAtS;
    bootInfo.sleepVolts = rtcSleepVolts;
    bootInfo.sleepReason = rtcSleepReason;
  }
  rtcSleepAtS = 0;
}

const BootInfo &lastBoot() { return bootInfo; }

void recordBoot(float bootVolts) {
  BootLogEntry log[kBootLogSize] = {};
  Preferences prefs;
  prefs.begin("bootlog", false);
  prefs.getBytes("log", log, sizeof(log));
  uint8_t next = prefs.getUChar("next", 0) % kBootLogSize;
  BootLogEntry &e = log[next];
  e.epoch = (uint32_t)nowSeconds();
  e.sleptS = (int32_t)bootInfo.sleptSeconds;
  e.sleepMv = (uint16_t)(bootInfo.sleepVolts * 1000);
  e.bootMv = (uint16_t)(bootVolts * 1000);
  e.reset = (uint8_t)bootReset;
  e.wake = bootWake;
  e.sleepWhy = bootInfo.sleptSeconds < 0 ? 0
               : strcmp(bootInfo.sleepReason, "Idle timeout reached") == 0 ? 1
               : strcmp(bootInfo.sleepReason, "Battery low") == 0 ? 2
               : strcmp(bootInfo.sleepReason, "No Wi-Fi") == 0 ? 3 : 0;
  prefs.putBytes("log", log, sizeof(log));
  prefs.putUChar("next", (next + 1) % kBootLogSize);
  prefs.end();
}

String bootLogJson() {
  static const char *kWake[] = {"none", "knob", "touch", "timer"};
  static const char *kWhy[] = {"", "idle", "battery low", "no wi-fi"};
  BootLogEntry log[kBootLogSize] = {};
  Preferences prefs;
  prefs.begin("bootlog", true);
  prefs.getBytes("log", log, sizeof(log));
  uint8_t next = prefs.getUChar("next", 0) % kBootLogSize;
  prefs.end();
  String out = "[";
  for (int i = 1; i <= kBootLogSize; i++) { // newest first
    const BootLogEntry &e = log[(next - i + kBootLogSize) % kBootLogSize];
    if (!e.epoch && !e.bootMv) continue;
    char when[24];
    time_t t = e.epoch;
    if (t > 1700000000) {
      strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    } else {
      snprintf(when, sizeof(when), "+%lus", (unsigned long)e.epoch);
    }
    char item[200];
    snprintf(item, sizeof(item),
             "%s{\"time\":\"%s\",\"reason\":\"%s\",\"wake\":\"%s\",\"slept_s\":%ld,"
             "\"sleep_v\":%.2f,\"sleep_why\":\"%s\",\"boot_v\":%.2f}",
             out.length() > 1 ? "," : "", when, resetReasonName((esp_reset_reason_t)e.reset),
             kWake[e.wake & 3], (long)e.sleptS, e.sleepMv / 1000.0f, kWhy[e.sleepWhy & 3], e.bootMv / 1000.0f);
    out += item;
  }
  return out + "]";
}

void sleepNow(const char *reason, uint32_t wakeAfterS) { enterDeepSleep(reason, wakeAfterS); }

void setBeforeSleep(void (*cb)()) { beforeSleep = cb; }

void noteActivity() {
  lastActivityMs = millis();
  if (dimmed) {
    display::setDimmed(false);
    dimmed = false;
  }
  if (!screenOn) {
    if (screenCallback) screenCallback(true);
    display::setBacklightOn(true);
    screenOn = true;
  }
}

void service() {
  uint32_t idleMs = millis() - lastActivityMs;
  // On USB power the screen stays on, dimmed, where it would otherwise turn off.
  uint32_t dimAtMs = externalPower ? screenTimeoutMs
                                   : (screenTimeoutMs > kDimLeadMs * 2 ? screenTimeoutMs - kDimLeadMs : screenTimeoutMs / 2);
  if (screenOn && !dimmed && idleMs > dimAtMs) {
    display::setDimmed(true);
    dimmed = true;
  }
  if (externalPower) return;

  if (screenOn && idleMs > screenTimeoutMs) {
    display::setBacklightOn(false);
    screenOn = false;
    if (screenCallback) screenCallback(false);
  }

  if (idleMs > sleepTimeoutMs) {
    enterDeepSleep("Idle timeout reached");
  }
}

bool isScreenOn() { return screenOn; }

bool isDimmed() { return dimmed; }

void setScreenCallback(void (*cb)(bool on)) { screenCallback = cb; }

void setScreenTimeout(uint32_t seconds) { screenTimeoutMs = seconds * 1000; }

void setSleepTimeout(uint32_t minutes) { sleepTimeoutMs = minutes * 60000UL; }

void setExternalPower(bool on) {
  externalPower = on;
  noteActivity(); // wakes the screen on plug-in; restarts the timeouts on unplug
}

} // namespace power
