#include "Thumbs.h"
#include "Roon.h"

#include <HTTPClient.h>
#include <JPEGDEC.h>
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
constexpr size_t kMaxJpeg = 32 * 1024;

struct Slot {
  char key[48];
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
uint8_t *jpegBuf = nullptr;
uint16_t *decodeTarget = nullptr;

int drawCallback(JPEGDRAW *d) {
  for (int row = 0; row < d->iHeight; row++) {
    int y = d->y + row;
    if (y < 0 || y >= kSize || d->x >= kSize) continue;
    int w = min(d->iWidthUsed, kSize - d->x);
    memcpy(&decodeTarget[y * kSize + d->x], &d->pPixels[row * d->iWidth], w * 2);
  }
  return 1;
}

bool fetch(const char *key, uint16_t *pixels) {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, imageUrl(key, kSize))) return false;
  http.setTimeout(4000);
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  NetworkClient *stream = http.getStreamPtr();
  size_t len = 0;
  uint32_t lastByteAt = millis();
  while (http.connected() && len < kMaxJpeg) {
    size_t avail = stream->available();
    if (!avail) {
      if (!stream->connected() || millis() - lastByteAt > 4000) break;
      delay(2);
      continue;
    }
    int got = stream->readBytes(jpegBuf + len, min(avail, kMaxJpeg - len));
    if (got <= 0) break;
    len += got;
    lastByteAt = millis();
  }
  http.end();
  if (!len || jpeg->openRAM(jpegBuf, len, drawCallback) != 1) return false;
  memset(pixels, 0, kSize * kSize * 2);
  decodeTarget = pixels;
  jpeg->setPixelType(RGB565_BIG_ENDIAN); // LV_COLOR_16_SWAP
  bool ok = jpeg->decode(0, 0, 0) == 1;
  jpeg->close();
  return ok;
}

void taskFn(void *) {
  for (;;) {
    int next = -1;
    char key[48];
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
  jpegBuf = (uint8_t *)heap_caps_malloc(kMaxJpeg, MALLOC_CAP_SPIRAM);
  void *mem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
  if (!slots || !jpegBuf || !mem) return;
  jpeg = new (mem) JPEGDEC();
  for (int i = 0; i < kSlots; i++) {
    Slot &s = slots[i];
    s.pixels = (uint16_t *)heap_caps_malloc(kSize * kSize * 2, MALLOC_CAP_SPIRAM);
    s.desc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s.desc.header.w = kSize;
    s.desc.header.h = kSize;
    s.desc.data_size = kSize * kSize * 2;
    s.desc.data = (const uint8_t *)s.pixels;
  }
  xTaskCreatePinnedToCore(taskFn, "roon-img", 6144, nullptr, 1, nullptr, 0);
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
