#include "TuneIn.h"
#include "wiim/WiimTask.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

namespace tunein {

namespace {

constexpr const char *kHost = "http://opml.radiotime.com/";
constexpr int kMaxDepth = 8;

struct SpiRamAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} spiRam;

struct Level {
  char query[96];
  char title[64];
};

struct ListState {
  Level levels[kMaxDepth];
  int depth = 1;
  Row *rows = nullptr;
  int count = 0;
  State state = State::kIdle;
};

struct Command {
  enum Kind : uint8_t { kLoad, kOpen, kBack } kind;
  uint8_t list;
  int16_t index;
};

SemaphoreHandle_t mutex;
QueueHandle_t commands;
ListState lists[kListCount];
Row *scratch = nullptr;
volatile uint32_t versionCounter = 0;

bool fetchJson(const String &query, JsonDocument &doc, const JsonDocument &filter) {
  NetworkClient plain;
  HTTPClient http;
  String url = kHost + query + (query.indexOf('?') >= 0 ? "&" : "?") + "render=json";
  if (!http.begin(plain, url)) return false;
  http.useHTTP10(true); // TuneIn answers HTTP/1.1 chunked, which getStream() doesn't undo
  http.setTimeout(6000);
  bool ok = http.GET() == HTTP_CODE_OK &&
            !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  return ok;
}

// Skips podcasts and shows: episodes need seek and a queue the knob doesn't have.
void addRows(JsonArrayConst items, Row *out, int &count) {
  for (JsonObjectConst item : items) {
    if (count >= kMaxRows) return;
    if (item["children"].is<JsonArrayConst>()) {
      addRows(item["children"], out, count);
      continue;
    }
    const char *type = item["type"] | "";
    const char *kind = item["item"] | "";
    const char *link = item["URL"] | "";
    bool station = strcmp(type, "audio") == 0 && strcmp(kind, "station") == 0;
    bool browse = strcmp(type, "link") == 0 && strstr(link, "Browse.ashx") && strcmp(kind, "show") != 0 &&
                  strcmp(kind, "topic") != 0 && strcmp(item["key"] | "", "podcast") != 0;
    if (!station && !browse) continue;
    Row &row = out[count++];
    memset(&row, 0, sizeof(row));
    row.station = station;
    strlcpy(row.title, item["text"] | "", sizeof(row.title));
    strlcpy(row.subtitle, station ? (item["subtext"] | "") : "", sizeof(row.subtitle));
    strlcpy(row.image, item["image"] | "", sizeof(row.image));
    if (station) {
      strlcpy(row.target, item["guide_id"] | "", sizeof(row.target));
    } else {
      const char *query = strstr(link, "Browse.ashx");
      strlcpy(row.target, query, sizeof(row.target));
    }
  }
}

void load(int which) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  ListState &list = lists[which];
  String query = list.levels[list.depth - 1].query;
  list.state = State::kLoading;
  xSemaphoreGive(mutex);
  versionCounter++;

  static JsonDocument filter;
  if (filter.isNull()) {
    JsonObject f = filter["body"][0].to<JsonObject>();
    for (const char *key : {"type", "item", "text", "subtext", "URL", "image", "guide_id", "key"}) f[key] = true;
    JsonObject c = f["children"][0].to<JsonObject>();
    for (const char *key : {"type", "item", "text", "subtext", "URL", "image", "guide_id", "key"}) c[key] = true;
  }
  JsonDocument doc(&spiRam);
  int count = 0;
  bool ok = WiFi.status() == WL_CONNECTED && fetchJson(query, doc, filter);
  if (ok) addRows(doc["body"], scratch, count);

  xSemaphoreTake(mutex, portMAX_DELAY);
  if (String(list.levels[list.depth - 1].query) == query) { // still the level that was asked for
    if (ok) {
      memcpy(list.rows, scratch, count * sizeof(Row));
      list.count = count;
    }
    list.state = ok ? State::kReady : State::kFailed;
  }
  xSemaphoreGive(mutex);
  versionCounter++;
}

void play(const Row &row) {
  static JsonDocument filter;
  if (filter.isNull()) filter["body"][0]["url"] = true;
  JsonDocument doc(&spiRam);
  if (!fetchJson(String("Tune.ashx?formats=mp3,aac&id=") + row.target, doc, filter)) {
    Serial.printf("[tunein] tune %s failed\n", row.target);
    return;
  }
  String url = firstStreamIn(doc["body"][0]["url"] | "");
  if (!url.length()) {
    Serial.printf("[tunein] %s has no stream\n", row.target);
    return;
  }
  wiim::task::requestStream(url, row.title, row.subtitle, row.image);
}

void taskFn(void *) {
  for (;;) {
    Command cmd;
    if (xQueueReceive(commands, &cmd, portMAX_DELAY) != pdTRUE) continue;
    ListState &list = lists[cmd.list];
    switch (cmd.kind) {
    case Command::kLoad:
      if (list.state == State::kIdle || list.state == State::kFailed) load(cmd.list);
      break;
    case Command::kBack:
      xSemaphoreTake(mutex, portMAX_DELAY);
      if (list.depth > 1) list.depth--;
      xSemaphoreGive(mutex);
      load(cmd.list);
      break;
    case Command::kOpen: {
      xSemaphoreTake(mutex, portMAX_DELAY);
      bool valid = cmd.index >= 0 && cmd.index < list.count;
      Row row;
      if (valid) row = list.rows[cmd.index];
      bool push = valid && !row.station && list.depth < kMaxDepth;
      if (push) {
        Level &level = list.levels[list.depth++];
        strlcpy(level.query, row.target, sizeof(level.query));
        strlcpy(level.title, row.title, sizeof(level.title));
      }
      xSemaphoreGive(mutex);
      if (push) load(cmd.list);
      else if (valid && row.station) play(row);
      break;
    }
    }
  }
}

void send(Command::Kind kind, List which, int index) {
  if (!commands) return;
  Command cmd = {kind, (uint8_t)which, (int16_t)index};
  xQueueSend(commands, &cmd, 0);
}

} // namespace

void begin() {
  mutex = xSemaphoreCreateMutex();
  commands = xQueueCreate(8, sizeof(Command));
  scratch = (Row *)heap_caps_calloc(kMaxRows, sizeof(Row), MALLOC_CAP_SPIRAM);
  const char *roots[kListCount][2] = {{"Browse.ashx?c=local", "Local"}, {"Browse.ashx", "Browse"}};
  for (int i = 0; i < kListCount; i++) {
    lists[i].rows = (Row *)heap_caps_calloc(kMaxRows, sizeof(Row), MALLOC_CAP_SPIRAM);
    strlcpy(lists[i].levels[0].query, roots[i][0], sizeof(Level::query));
    strlcpy(lists[i].levels[0].title, roots[i][1], sizeof(Level::title));
  }
  // PSRAM stack: safe only because this task never writes flash (no NVS, no LittleFS)
  xTaskCreatePinnedToCoreWithCaps(taskFn, "tunein", 8192, nullptr, 1, nullptr, 0, MALLOC_CAP_SPIRAM);
}

void requestLoad(List which) { send(Command::kLoad, which, 0); }
void requestOpen(List which, int index) { send(Command::kOpen, which, index); }
void requestBack(List which) { send(Command::kBack, which, 0); }

int copyList(List which, Row *out, int max) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const ListState &list = lists[(int)which];
  int n = min(max, list.count);
  memcpy(out, list.rows, n * sizeof(Row));
  xSemaphoreGive(mutex);
  return n;
}

State state(List which) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  State s = lists[(int)which].state;
  xSemaphoreGive(mutex);
  return s;
}

String parentTitle(List which) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const ListState &list = lists[(int)which];
  String title = list.depth > 1 ? list.levels[list.depth - 2].title : "";
  xSemaphoreGive(mutex);
  return title;
}

uint32_t version() { return versionCounter; }

String firstStreamIn(const String &url) {
  String path = url;
  path.toLowerCase();
  if (path.indexOf('?') >= 0) path.remove(path.indexOf('?'));
  if (!url.startsWith("http://") || !(path.endsWith(".pls") || path.endsWith(".m3u"))) return url;
  NetworkClient plain;
  HTTPClient http;
  if (!http.begin(plain, url)) return url;
  http.setTimeout(5000);
  String body = http.GET() == HTTP_CODE_OK ? http.getString() : String();
  http.end();
  int from = 0;
  while (from < (int)body.length()) {
    int end = body.indexOf('\n', from);
    if (end < 0) end = body.length();
    String line = body.substring(from, end);
    line.trim();
    if (line.startsWith("File")) line.remove(0, line.indexOf('=') + 1);
    if (line.startsWith("http")) return line;
    from = end + 1;
  }
  return url;
}

} // namespace tunein
