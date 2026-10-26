#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Small covers for list rows, cached in PSRAM. A key is a Roon image key, fetched from Roon at
// its final size, or an http:// URL to a JPEG or PNG of any size, center-cropped and scaled.
namespace roon {
namespace thumbs {

constexpr int kSize = 48;
constexpr int kKeyLen = 160;

void begin();
// nullptr until it's downloaded; asking queues the download.
const lv_img_dsc_t *get(const char *imageKey);
// Bumped whenever a thumbnail finishes.
uint32_t version();

} // namespace thumbs
} // namespace roon
