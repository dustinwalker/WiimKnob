#include "BatteryCal.h"
#include "Battery.h"

#include <LittleFS.h>

namespace power {
namespace batcal {

namespace {
constexpr const char *kLogPath = "/batcal.bin"; // one uint16 mV per minute, appended
constexpr uint32_t kSampleMs = 60000;
constexpr uint32_t kMinMinutes = 30;
constexpr uint16_t kNearEmptyMv = 3600; // last sample below this: the run got to the end
constexpr uint16_t kRechargedMv = 150;  // reading this far above it: charged since
bool on = false;
uint32_t lastSampleMs = 0;
uint32_t minutes = 0;

uint16_t lastLoggedMv() {
  if (!minutes || !LittleFS.begin(true)) return 0;
  File f = LittleFS.open(kLogPath, "r");
  if (!f || f.size() < 2) return 0;
  uint16_t mv = 0;
  f.seek(f.size() - 2);
  f.read((uint8_t *)&mv, 2);
  f.close();
  return mv;
}

void discard() {
  if (LittleFS.begin(true)) LittleFS.remove(kLogPath);
  minutes = 0;
}
} // namespace

void setEnabled(bool enable) {
  if (enable == on) return;
  on = enable;
  if (!on) {
    discard();
    return;
  }
  // A run survives reboots: pick up where the log left off.
  minutes = 0;
  if (LittleFS.begin(true)) {
    File f = LittleFS.open(kLogPath, "r");
    if (f) {
      minutes = f.size() / 2;
      f.close();
    }
  }
}

bool enabled() { return on; }

uint32_t loggedMinutes() { return minutes; }

Result sample(float volts, bool externalPower) {
  if (!on) return Result::kNone;
  if (externalPower) {
    if (minutes == 0) return Result::kNone; // not unplugged yet: wait
    uint16_t last = lastLoggedMv();
    if (last && last < kNearEmptyMv) return finish() ? Result::kFinished : Result::kAborted;
    discard();
    on = false;
    return Result::kAborted;
  }
  uint32_t now = millis();
  if (minutes && now - lastSampleMs < kSampleMs) return Result::kNone;
  lastSampleMs = now;
  if (!LittleFS.begin(true)) return Result::kNone;
  File f = LittleFS.open(kLogPath, "a");
  if (!f) return Result::kNone;
  uint16_t mv = (uint16_t)(volts * 1000);
  f.write((const uint8_t *)&mv, 2);
  f.close();
  minutes++;
  return Result::kNone;
}

bool endedEarlier(float volts, bool externalPower) {
  uint16_t last = lastLoggedMv();
  return on && last && last < kNearEmptyMv && (externalPower || volts * 1000 > last + kRechargedMv);
}

bool finish() {
  if (!on || minutes < kMinMinutes || !LittleFS.begin(true)) {
    discard();
    on = false;
    return false;
  }
  File f = LittleFS.open(kLogPath, "r");
  size_t n = f ? f.size() / 2 : 0;
  uint16_t *log = n ? (uint16_t *)ps_malloc(n * 2) : nullptr;
  bool ok = log && f.read((uint8_t *)log, n * 2) == n * 2;
  if (f) f.close();
  if (ok) {
    // At a steady load, elapsed time is charge used: the voltage k/10 of the way through the run
    // is the (100 - 10k)% point. Each point averages a few minutes either side against ADC noise,
    // and the curve is forced to never rise.
    uint16_t curve[battery::kCurvePoints];
    for (int k = 0; k < battery::kCurvePoints; k++) {
      int mid = (int)((size_t)k * (n - 1) / (battery::kCurvePoints - 1));
      uint32_t sum = 0, count = 0;
      for (int i = max(0, mid - 2); i <= min((int)n - 1, mid + 2); i++, count++) sum += log[i];
      curve[k] = sum / count;
      if (k && curve[k] > curve[k - 1]) curve[k] = curve[k - 1];
    }
    battery::setLearnedCurve(curve, n);
    // Shut down ~3% of the run before where it actually gave out, so "Battery low" comes with
    // charge to spare instead of a brownout.
    size_t at = n - 1 - max<size_t>(1, n * 3 / 100);
    battery::setLearnedCutoff(log[at]);
  }
  free(log);
  discard();
  on = false;
  return ok;
}

} // namespace batcal
} // namespace power
