#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Small covers for the Library rows, fetched from Roon at their final size and cached in PSRAM.
namespace roon {
namespace thumbs {

constexpr int kSize = 48;

void begin();
// nullptr until it's downloaded; asking queues the download.
const lv_img_dsc_t *get(const char *imageKey);
// Bumped whenever a thumbnail finishes.
uint32_t version();

} // namespace thumbs
} // namespace roon
