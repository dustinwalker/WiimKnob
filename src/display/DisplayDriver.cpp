#include "DisplayDriver.h"
#include "PanelInit.h"
#include "Pins.h"

#include <Arduino_GFX_Library.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

namespace display {

namespace {

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;

lv_disp_draw_buf_t drawBuf;
lv_disp_drv_t dispDrv;
lv_color_t *buf1 = nullptr;
lv_color_t *buf2 = nullptr;

// A tall partial buffer keeps LVGL's redraw cost low without needing a full 360x360 frame
// (which would be 253KB per buffer -- fine in PSRAM, but unnecessary for this UI).
constexpr int kBufRows = 60;

constexpr uint32_t kBacklightPwmHz = 20000; // above hearing, and too fast to see flicker
uint8_t brightnessPercent = 100;
bool backlightOn = false;

// Pixels go out on our own SPI device from a task on the other core, so LVGL renders the next
// band while this one transfers. Arduino_GFX's own path sends 2KB at a time and waits on each,
// which cost ~17ms of every full-screen frame; it's only used for the panel's init sequence now.
// DMA can't read LVGL's PSRAM buffers directly at this clock (TX underflow), so each band is
// copied through two small internal bounce buffers, one filling while the other sends.
spi_device_handle_t pixelDev = nullptr;
constexpr size_t kBounceBytes = 8192;
uint8_t *bounce[2] = {};
spi_transaction_t trans[2];
bool pending[2] = {};

struct FlushJob {
  lv_disp_drv_t *drv;
  lv_area_t area;
  const uint8_t *pixels;
};
QueueHandle_t jobs = nullptr;

void sendCommand(uint8_t cmd, const uint8_t *data, int len) {
  spi_transaction_t t = {};
  t.cmd = 0x02;
  t.addr = (uint32_t)cmd << 8;
  t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
  if (len) {
    t.flags |= SPI_TRANS_USE_TXDATA;
    memcpy(t.tx_data, data, len);
    t.length = len * 8;
  }
  spi_device_polling_transmit(pixelDev, &t);
}

void waitFor(int i) {
  if (!pending[i]) return;
  spi_transaction_t *done;
  spi_device_get_trans_result(pixelDev, &done, portMAX_DELAY);
  pending[i] = false;
}

void flushTask(void *) {
  FlushJob job;
  for (;;) {
    xQueueReceive(jobs, &job, portMAX_DELAY);
    const lv_area_t &a = job.area;
    uint8_t caset[4] = {(uint8_t)(a.x1 >> 8), (uint8_t)a.x1, (uint8_t)(a.x2 >> 8), (uint8_t)a.x2};
    uint8_t raset[4] = {(uint8_t)(a.y1 >> 8), (uint8_t)a.y1, (uint8_t)(a.y2 >> 8), (uint8_t)a.y2};
    sendCommand(0x2A, caset, 4);
    sendCommand(0x2B, raset, 4);
    sendCommand(0x2C, nullptr, 0);

    size_t remaining = (size_t)lv_area_get_size(&a) * sizeof(lv_color_t);
    const uint8_t *p = job.pixels;
    int i = 0;
    while (remaining) {
      size_t len = remaining > kBounceBytes ? kBounceBytes : remaining;
      waitFor(i); // transactions complete in order, so this is the one queued on bounce[i]
      memcpy(bounce[i], p, len);
      spi_transaction_t &t = trans[i];
      t = {};
      t.cmd = 0x32;
      t.addr = 0x003C00; // RAMWRC: continue where the previous chunk stopped
      t.flags = SPI_TRANS_MODE_QIO;
      t.tx_buffer = bounce[i];
      t.length = len * 8;
      spi_device_queue_trans(pixelDev, &t, portMAX_DELAY);
      pending[i] = true;
      remaining -= len;
      p += len;
      i ^= 1;
    }
    waitFor(i);
    waitFor(i ^ 1);
    lv_disp_flush_ready(job.drv);
  }
}

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  FlushJob job = {drv, *area, (const uint8_t *)color_p};
  xQueueSend(jobs, &job, portMAX_DELAY);
}

} // namespace

void begin() {
  ledcAttach(PIN_LCD_BACKLIGHT, kBacklightPwmHz, 8);
  ledcWrite(PIN_LCD_BACKLIGHT, 0); // keep the panel dark until it's initialised

  // Shared, so Arduino_GFX doesn't hold the bus and pixelDev can use it after init.
  bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
                              /*is_shared_interface=*/true);

  // Rotation index 2 makes Arduino_ST77916::setRotation() write MADCTL=0xC0, which is this
  // panel's verified mounting correction (see PanelInit.h and Pins.h for why).
  gfx = new Arduino_ST77916(
      bus, PIN_LCD_RST, /*r=*/2, /*ips=*/true, /*w=*/360, /*h=*/360,
      0, 0, 0, 0,
      st77916_knob_init_operations, sizeof(st77916_knob_init_operations));

  gfx->begin(80000000); // 80MHz QSPI clock -- verified stable on this panel

  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits = 8;
  devcfg.address_bits = 24;
  devcfg.mode = 0;
  devcfg.clock_source = SPI_CLK_SRC_DEFAULT;
  devcfg.clock_speed_hz = 80000000;
  devcfg.spics_io_num = PIN_LCD_CS;
  devcfg.flags = SPI_DEVICE_HALFDUPLEX;
  devcfg.queue_size = 2;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &pixelDev));
  for (auto &b : bounce) {
    b = (uint8_t *)heap_caps_malloc(kBounceBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  }
  jobs = xQueueCreate(1, sizeof(FlushJob));
  xTaskCreatePinnedToCore(flushTask, "lcdflush", 3072, nullptr, configMAX_PRIORITIES - 2, nullptr, 0);

  setBacklightOn(true);

  lv_init();

  size_t bufPixels = (size_t)360 * kBufRows;
  buf1 = (lv_color_t *)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  buf2 = (lv_color_t *)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, bufPixels);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = 360;
  dispDrv.ver_res = 360;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);
}

void tick() { lv_timer_handler(); }

void refreshNow() { lv_refr_now(NULL); }

void setBacklightOn(bool on) {
  backlightOn = on;
  // Squared so the low end of the percentage range gets most of the duty steps, roughly
  // matching how the eye perceives brightness.
  uint32_t duty = (uint32_t)brightnessPercent * brightnessPercent * 255 / 10000;
  ledcWrite(PIN_LCD_BACKLIGHT, on ? max<uint32_t>(duty, 2) : 0);
}

void setBrightness(uint8_t percent) {
  brightnessPercent = constrain(percent, 1, 100);
  if (backlightOn) setBacklightOn(true);
}

} // namespace display
