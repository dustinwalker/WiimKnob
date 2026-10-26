#include "Thumbs.h"
#include "Roon.h"

#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <new>

namespace roon {
namespace thumbs {

namespace {

// Above the 100-row lists' combined need, so a shown thumbnail is rarely evicted and rewritten.
constexpr int kSlots = 160;
constexpr size_t kMaxImage = 64 * 1024;
constexpr int kMaxSide = 512; // decode scratch; bigger JPEGs decode scaled down, bigger PNGs are skipped

struct Slot {
  char key[kKeyLen];
  uint16_t *pixels;
  lv_img_dsc_t desc;
  bool ready;
  bool queued;
  uint32_t lastUse;
};

Slot *slots = nullptr;
SemaphoreHandle_t mutex;
volatile uint32_t versionCounter = 0;
uint32_t useCounter = 0;
JPEGDEC *jpeg = nullptr;
PNG *png = nullptr;
uint8_t *imageBuf = nullptr;
uint16_t *scratch = nullptr; // kMaxSide x kMaxSide, little-endian RGB565
int scratchW = 0, scratchH = 0;

int jpegDraw(JPEGDRAW *d) {
  for (int row = 0; row < d->iHeight; row++) {
    int y = d->y + row;
    if (y < 0 || y >= scratchH || d->x >= scratchW) continue;
    int w = min(d->iWidthUsed, scratchW - d->x);
    memcpy(&scratch[y * scratchW + d->x], &d->pPixels[row * d->iWidth], w * 2);
  }
  return 1;
}

int pngDraw(PNGDRAW *d) {
  if (d->y < scratchH) png->getLineAsRGB565(d, &scratch[d->y * scratchW], PNG_RGB565_LITTLE_ENDIAN, 0x00ffffff);
  return 1;
}

bool decodeJpeg(size_t len) {
  if (jpeg->openRAM(imageBuf, len, jpegDraw) != 1) return false;
  int w = jpeg->getWidth(), h = jpeg->getHeight(), shift = 0;
  while (shift < 3 && (w >> shift > kMaxSide || h >> shift > kMaxSide)) shift++;
  scratchW = w >> shift;
  scratchH = h >> shift;
  bool ok = scratchW <= kMaxSide && scratchH <= kMaxSide;
  if (ok) {
    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
    const int options[] = {0, JPEG_SCALE_HALF, JPEG_SCALE_QUARTER, JPEG_SCALE_EIGHTH};
    ok = jpeg->decode(0, 0, options[shift]) == 1;
  }
  jpeg->close();
  return ok;
}

bool decodePng(size_t len) {
  if (png->openRAM(imageBuf, len, pngDraw) != PNG_SUCCESS) return false;
  scratchW = png->getWidth();
  scratchH = png->getHeight();
  bool ok = scratchW <= kMaxSide && scratchH <= kMaxSide && png->decode(nullptr, 0) == PNG_SUCCESS;
  png->close();
  return ok;
}

// Box-filters the center square of the scratch image down to kSize, byte-swapped for
// LV_COLOR_16_SWAP.
void downscale(uint16_t *pixels) {
  int side = min(scratchW, scratchH);
  int ox = (scratchW - side) / 2, oy = (scratchH - side) / 2;
  for (int y = 0; y < kSize; y++) {
    int y0 = oy + y * side / kSize, y1 = max(y0 + 1, oy + (y + 1) * side / kSize);
    for (int x = 0; x < kSize; x++) {
      int x0 = ox + x * side / kSize, x1 = max(x0 + 1, ox + (x + 1) * side / kSize);
      uint32_t r = 0, g = 0, b = 0, n = 0;
      for (int sy = y0; sy < y1; sy++) {
        for (int sx = x0; sx < x1; sx++) {
          uint16_t c = scratch[sy * scratchW + sx];
          r += c >> 11;
          g += (c >> 5) & 0x3f;
          b += c & 0x1f;
          n++;
        }
      }
      uint16_t c = (r / n) << 11 | (g / n) << 5 | (b / n);
      pixels[y * kSize + x] = c << 8 | c >> 8;
    }
  }
}

bool fetch(const char *key, uint16_t *pixels) {
  bool isUrl = strncmp(key, "http://", 7) == 0; // https would need a TLS session
  if (!isUrl && strncmp(key, "https://", 8) == 0) return false;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, isUrl ? String(key) : imageUrl(key, kSize))) return false;
  http.setTimeout(4000);
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  NetworkClient *stream = http.getStreamPtr();
  size_t len = 0;
  uint32_t lastByteAt = millis();
  while (http.connected() && len < kMaxImage) {
    size_t avail = stream->available();
    if (!avail) {
      if (!stream->connected() || millis() - lastByteAt > 4000) break;
      delay(2);
      continue;
    }
    int got = stream->readBytes(imageBuf + len, min(avail, kMaxImage - len));
    if (got <= 0) break;
    len += got;
    lastByteAt = millis();
  }
  http.end();
  if (len < 8 || len >= kMaxImage) return false;
  bool isPng = memcmp(imageBuf, "\x89PNG", 4) == 0;
  memset(scratch, 0, kMaxSide * kMaxSide * 2);
  if (!(isPng ? decodePng(len) : decodeJpeg(len))) return false;
  downscale(pixels);
  return true;
}

void taskFn(void *) {
  for (;;) {
    int next = -1;
    char key[kKeyLen];
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < kSlots; i++) {
      if (slots[i].queued && (next < 0 || slots[i].lastUse < slots[next].lastUse)) next = i; // top rows first
    }
    if (next >= 0) strlcpy(key, slots[next].key, sizeof(key));
    xSemaphoreGive(mutex);
    if (next < 0 || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    bool ok = fetch(key, slots[next].pixels);
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (strcmp(slots[next].key, key) == 0) {
      slots[next].queued = false;
      slots[next].ready = ok;
    }
    xSemaphoreGive(mutex);
    if (ok) versionCounter++;
    else vTaskDelay(pdMS_TO_TICKS(500));
  }
}

} // namespace

void begin() {
  mutex = xSemaphoreCreateMutex();
  slots = (Slot *)heap_caps_calloc(kSlots, sizeof(Slot), MALLOC_CAP_SPIRAM);
  imageBuf = (uint8_t *)heap_caps_malloc(kMaxImage, MALLOC_CAP_SPIRAM);
  scratch = (uint16_t *)heap_caps_malloc(kMaxSide * kMaxSide * 2, MALLOC_CAP_SPIRAM);
  void *jpegMem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
  void *pngMem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM); // ~48KB of inflate state
  if (!slots || !imageBuf || !scratch || !jpegMem || !pngMem) return;
  jpeg = new (jpegMem) JPEGDEC();
  png = new (pngMem) PNG();
  for (int i = 0; i < kSlots; i++) {
    Slot &s = slots[i];
    s.pixels = (uint16_t *)heap_caps_malloc(kSize * kSize * 2, MALLOC_CAP_SPIRAM);
    s.desc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s.desc.header.w = kSize;
    s.desc.header.h = kSize;
    s.desc.data_size = kSize * kSize * 2;
    s.desc.data = (const uint8_t *)s.pixels;
  }
  xTaskCreatePinnedToCore(taskFn, "roon-img", 8192, nullptr, 1, nullptr, 0);
}

const lv_img_dsc_t *get(const char *imageKey) {
  if (!slots || !imageKey || !*imageKey) return nullptr;
  xSemaphoreTake(mutex, portMAX_DELAY);
  int lru = 0;
  for (int i = 0; i < kSlots; i++) {
    Slot &s = slots[i];
    if (strcmp(s.key, imageKey) == 0) {
      s.lastUse = ++useCounter;
      const lv_img_dsc_t *d = s.ready ? &s.desc : nullptr;
      xSemaphoreGive(mutex);
      return d;
    }
    if (s.lastUse < slots[lru].lastUse) lru = i;
  }
  Slot &s = slots[lru];
  strlcpy(s.key, imageKey, sizeof(s.key));
  s.ready = false;
  s.queued = true;
  s.lastUse = ++useCounter;
  xSemaphoreGive(mutex);
  return nullptr;
}

uint32_t version() { return versionCounter; }

} // namespace thumbs
} // namespace roon
