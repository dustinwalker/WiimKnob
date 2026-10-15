#include "Roon.h"
#include "WsClient.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <time.h>
#include <vector>

namespace roon {

namespace {

constexpr const char *kServiceId = "00720724-5143-4a9b-abac-0e50cba674bb";
constexpr const char *kBrowse = "com.roonlabs.browse:1/browse";
constexpr const char *kLoad = "com.roonlabs.browse:1/load";
constexpr const char *kHistoryPath = "/roon_history.json";
constexpr uint16_t kSoodPort = 9003;
constexpr uint32_t kPingEveryMs = 10000;
constexpr uint32_t kDeadAfterMs = 35000;
constexpr uint32_t kRetryMs = 5000;
constexpr uint32_t kListsRefreshMs = 10 * 60 * 1000;
constexpr uint32_t kHistorySaveDelayMs = 5000;

enum class Link { kIdle, kSearching, kConnecting, kAwaitingApproval, kConnected };

struct SpiRamAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} spiRam;

struct Command {
  enum Kind : uint8_t { kPlay, kControl, kRefresh } kind;
  uint8_t list;
  int16_t index;
  char control[12];
};

struct Parsed {
  String verb;
  String name;
  uint32_t requestId = 0;
  String rawRequestId;
  const uint8_t *body = nullptr;
  size_t bodyLen = 0;
};

SemaphoreHandle_t mutex;
QueueHandle_t commands;
WsClient ws;
volatile bool active = true;
Link link = Link::kIdle;
IPAddress coreIp;
uint16_t corePort = 0;
String coreName;
String extensionId;
String displayName;
String token;
String savedOutput;
std::vector<Zone> zones;
volatile uint32_t versionCounter = 0;
uint32_t nextRequestId = 0;
uint32_t registerRequestId = UINT32_MAX;
uint32_t zonesRequestId = UINT32_MAX;
volatile bool sourceMatched = false;

Row *rows[kListCount] = {};
int rowCount[kListCount] = {};
volatile uint32_t listsVersionCounter = 0;
bool historyDirty = false;
uint32_t historyChangedMs = 0;

void loadState() {
  Preferences prefs;
  prefs.begin("roon", true);
  // A token belongs to one extension id, so a renamed extension starts over with approval.
  token = prefs.getString("extid", "") == extensionId ? prefs.getString("token", "") : String();
  savedOutput = prefs.getString("output", "");
  coreIp.fromString(prefs.getString("host", ""));
  corePort = prefs.getUShort("port", 0);
  prefs.end();
}

void saveString(const char *key, const String &value) {
  Preferences prefs;
  prefs.begin("roon", false);
  prefs.putString(key, value);
  prefs.end();
}

void saveCore() {
  Preferences prefs;
  prefs.begin("roon", false);
  prefs.putString("host", coreIp.toString());
  prefs.putUShort("port", corePort);
  prefs.end();
}

void copyField(char *dst, size_t size, const char *src) { strlcpy(dst, src ? src : "", size); }

// History: newest first, kept in PSRAM and saved to flash a few seconds after it changes.
void loadHistory() {
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open(kHistoryPath, "r");
  if (!f) return;
  JsonDocument doc(&spiRam);
  if (deserializeJson(doc, f)) return;
  int n = 0;
  for (JsonArray e : doc.as<JsonArray>()) {
    if (n >= kMaxRows) break;
    Row &r = rows[(int)List::kHistory][n++];
    copyField(r.title, sizeof(r.title), e[0]);
    copyField(r.subtitle, sizeof(r.subtitle), e[1]);
    copyField(r.imageKey, sizeof(r.imageKey), e[2]);
  }
  rowCount[(int)List::kHistory] = n;
}

void saveHistory() {
  JsonDocument doc(&spiRam);
  xSemaphoreTake(mutex, portMAX_DELAY);
  for (int i = 0; i < rowCount[(int)List::kHistory]; i++) {
    const Row &r = rows[(int)List::kHistory][i];
    JsonArray e = doc.add<JsonArray>();
    e.add(r.title);
    e.add(r.subtitle);
    e.add(r.imageKey);
  }
  xSemaphoreGive(mutex);
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open(kHistoryPath, "w");
  if (!f) return;
  serializeJson(doc, f);
}

// Called with the mutex held.
void noteHistory(const Zone &z) {
  if (z.title.isEmpty()) return;
  Row *list = rows[(int)List::kHistory];
  int &count = rowCount[(int)List::kHistory];
  if (count && z.title == list[0].title && z.artist == list[0].subtitle) {
    if (!list[0].imageKey[0]) copyField(list[0].imageKey, sizeof(list[0].imageKey), z.imageKey.c_str());
    return;
  }
  if (count == kMaxRows) count--;
  memmove(&list[1], &list[0], count * sizeof(Row));
  count++;
  copyField(list[0].title, sizeof(list[0].title), z.title.c_str());
  copyField(list[0].subtitle, sizeof(list[0].subtitle), z.artist.c_str());
  copyField(list[0].imageKey, sizeof(list[0].imageKey), z.imageKey.c_str());
  listsVersionCounter++;
  historyDirty = true;
  historyChangedMs = millis();
}

void appendProp(uint8_t *buf, size_t &pos, const char *name, const String &value) {
  size_t n = strlen(name);
  buf[pos++] = n;
  memcpy(buf + pos, name, n);
  pos += n;
  buf[pos++] = value.length() >> 8;
  buf[pos++] = value.length() & 0xFF;
  memcpy(buf + pos, value.c_str(), value.length());
  pos += value.length();
}

// SOOD: Roon's discovery query, sent to multicast and broadcast; the Core answers by unicast.
bool discover() {
  WiFiUDP udp;
  if (!udp.begin(0)) return false;
  uint8_t pkt[160];
  size_t pos = 0;
  memcpy(pkt, "SOOD\x02Q", 6);
  pos = 6;
  appendProp(pkt, pos, "query_service_id", kServiceId);
  appendProp(pkt, pos, "_tid", String(esp_random(), HEX) + String(esp_random(), HEX));
  for (IPAddress dst : {IPAddress(239, 255, 90, 90), WiFi.broadcastIP()}) {
    udp.beginPacket(dst, kSoodPort);
    udp.write(pkt, pos);
    udp.endPacket();
  }

  uint32_t started = millis();
  while (millis() - started < 3000) {
    int size = udp.parsePacket();
    if (size <= 6) {
      delay(20);
      continue;
    }
    uint8_t reply[512];
    int len = udp.read(reply, sizeof(reply));
    if (len < 6 || memcmp(reply, "SOOD\x02R", 6) != 0) continue;
    String serviceId, httpPort, replyAddr;
    for (int p = 6; p < len;) {
      int nameLen = reply[p++];
      if (p + nameLen + 2 > len) break;
      String name((const char *)reply + p, nameLen);
      p += nameLen;
      int valueLen = (reply[p] << 8) | reply[p + 1];
      p += 2;
      if (valueLen == 0xFFFF) continue;
      if (p + valueLen > len) break;
      String value((const char *)reply + p, valueLen);
      p += valueLen;
      if (name == "service_id") serviceId = value;
      else if (name == "http_port") httpPort = value;
      else if (name == "_replyaddr") replyAddr = value;
    }
    if (serviceId != kServiceId || httpPort.isEmpty()) continue;
    coreIp = udp.remoteIP();
    if (replyAddr.length()) coreIp.fromString(replyAddr);
    corePort = httpPort.toInt();
    udp.stop();
    saveCore();
    Serial.printf("[roon] found core at %s:%u\n", coreIp.toString().c_str(), corePort);
    return true;
  }
  udp.stop();
  return false;
}

uint32_t sendRequest(const char *name, const String &json) {
  uint32_t id = nextRequestId++;
  String msg = String("MOO/1 REQUEST ") + name + "\nRequest-Id: " + id + "\n";
  if (json.length()) msg += "Content-Length: " + String(json.length()) + "\nContent-Type: application/json\n";
  msg += "\n" + json;
  ws.sendBinary((const uint8_t *)msg.c_str(), msg.length());
  return id;
}

void sendComplete(const String &requestId, const char *name) {
  String msg = String("MOO/1 COMPLETE ") + name + "\nRequest-Id: " + requestId + "\n\n";
  ws.sendBinary((const uint8_t *)msg.c_str(), msg.length());
}

void sendRegister() {
  JsonDocument doc;
  doc["extension_id"] = extensionId;
  doc["display_name"] = displayName;
  doc["display_version"] = "1.1";
  doc["publisher"] = "WiimKnob";
  doc["email"] = "";
  doc["website"] = "https://github.com/dustinwalker/WiimKnob";
  doc["required_services"].add("com.roonlabs.transport:2");
  doc["required_services"].add("com.roonlabs.image:1");
  doc["required_services"].add("com.roonlabs.browse:1");
  doc["optional_services"].to<JsonArray>();
  doc["provided_services"].add("com.roonlabs.ping:1");
  if (token.length()) doc["token"] = token;
  String json;
  serializeJson(doc, json);
  registerRequestId = sendRequest("com.roonlabs.registry:1/register", json);
}

Zone *findZone(const char *zoneId) {
  for (Zone &z : zones) {
    if (z.zoneId == zoneId) return &z;
  }
  return nullptr;
}

bool hasOutput(const Zone &z, const String &outputId) {
  if (outputId.isEmpty()) return false;
  return ("," + z.outputIds + ",").indexOf("," + outputId + ",") >= 0;
}

String firstOutput(const Zone &z) {
  int comma = z.outputIds.indexOf(',');
  return comma < 0 ? z.outputIds : z.outputIds.substring(0, comma);
}

// Called with the mutex held.
void applyZone(JsonObject src) {
  const char *id = src["zone_id"] | "";
  if (!*id) return;
  Zone *z = findZone(id);
  if (!z) {
    zones.push_back(Zone());
    z = &zones.back();
    z->zoneId = id;
  }
  z->name = src["display_name"] | "";
  z->state = src["state"] | "";
  z->outputIds = "";
  for (JsonObject o : src["outputs"].as<JsonArray>()) {
    if (z->outputIds.length()) z->outputIds += ",";
    z->outputIds += o["output_id"] | "";
  }
  JsonObject np = src["now_playing"];
  String title = np["three_line"]["line1"] | "";
  String imageKey = np["image_key"] | "";
  bool changed = title != z->title || imageKey != z->imageKey;
  if (title != z->title) z->prevTitle = z->title;
  z->title = title;
  z->artist = np["three_line"]["line2"] | "";
  z->album = np["three_line"]["line3"] | "";
  z->imageKey = imageKey;
  z->seekS = np["seek_position"] | -1L;
  z->lengthS = np["length"] | 0L;
  if (changed) {
    versionCounter++;
    if (hasOutput(*z, savedOutput)) noteHistory(*z);
  }
}

void handleZones(const char *name, const uint8_t *body, size_t len) {
  static JsonDocument filter;
  if (filter.isNull()) {
    JsonObject zone = filter["zones"][0].to<JsonObject>();
    zone["zone_id"] = true;
    zone["display_name"] = true;
    zone["state"] = true;
    zone["outputs"][0]["output_id"] = true;
    zone["now_playing"]["image_key"] = true;
    zone["now_playing"]["three_line"] = true;
    zone["now_playing"]["seek_position"] = true;
    zone["now_playing"]["length"] = true;
    filter["zones_added"][0] = zone;
    filter["zones_changed"][0] = zone;
    filter["zones_removed"] = true;
    filter["zones_seek_changed"][0]["zone_id"] = true;
    filter["zones_seek_changed"][0]["seek_position"] = true;
  }
  JsonDocument doc(&spiRam);
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter))) return;

  xSemaphoreTake(mutex, portMAX_DELAY);
  if (strcmp(name, "Subscribed") == 0) zones.clear();
  for (const char *key : {"zones", "zones_added", "zones_changed"}) {
    for (JsonObject z : doc[key].as<JsonArray>()) applyZone(z);
  }
  for (JsonObject s : doc["zones_seek_changed"].as<JsonArray>()) {
    if (Zone *z = findZone(s["zone_id"] | "")) z->seekS = s["seek_position"] | -1L;
  }
  for (const char *id : doc["zones_removed"].as<JsonArray>()) {
    for (size_t i = 0; i < zones.size(); i++) {
      if (zones[i].zoneId == id) zones.erase(zones.begin() + i--);
    }
  }
  xSemaphoreGive(mutex);
}

bool parse(const uint8_t *msg, size_t len, Parsed &out) {
  const uint8_t *end = msg + len;
  const uint8_t *eol = (const uint8_t *)memchr(msg, '\n', len);
  if (!eol || len < 7 || memcmp(msg, "MOO/1 ", 6) != 0) return false;
  String first((const char *)msg + 6, eol - msg - 6);
  int space = first.indexOf(' ');
  out.verb = first.substring(0, space);
  out.name = first.substring(space + 1);
  size_t contentLength = 0;
  const uint8_t *p = eol + 1;
  while (p < end) {
    const uint8_t *lineEnd = (const uint8_t *)memchr(p, '\n', end - p);
    if (!lineEnd) return false;
    if (lineEnd == p) {
      p++;
      break;
    }
    String line((const char *)p, lineEnd - p);
    if (line.startsWith("Request-Id: ")) out.rawRequestId = line.substring(12);
    else if (line.startsWith("Content-Length: ")) contentLength = line.substring(16).toInt();
    p = lineEnd + 1;
  }
  out.requestId = out.rawRequestId.toInt();
  out.body = p;
  out.bodyLen = min(contentLength, (size_t)(end - p));
  return true;
}

void dispatch(const Parsed &m) {
  if (m.verb == "REQUEST") {
    sendComplete(m.rawRequestId, m.name == "com.roonlabs.ping:1/ping" ? "Success" : "InvalidRequest");
    return;
  }
  if (m.requestId == registerRequestId) {
    if (m.name != "Registered") {
      Serial.printf("[roon] register: %s\n", m.name.c_str());
      return;
    }
    JsonDocument doc(&spiRam);
    if (deserializeJson(doc, m.body, m.bodyLen)) return;
    String newToken = doc["token"] | "";
    if (newToken.length() && newToken != token) {
      token = newToken;
      saveString("token", token);
      saveString("extid", extensionId);
    }
    coreName = doc["display_name"] | "Roon";
    link = Link::kConnected;
    Serial.printf("[roon] registered with %s\n", coreName.c_str());
    zonesRequestId = sendRequest("com.roonlabs.transport:2/subscribe_zones", "{\"subscription_key\":0}");
    Command refresh = {Command::kRefresh, 0, 0, ""};
    xQueueSend(commands, &refresh, 0);
  } else if (m.requestId == zonesRequestId) {
    handleZones(m.name.c_str(), m.body, m.bodyLen);
  }
}

// Sends a request and keeps handling everything else (pings, zone updates) until its answer comes.
bool call(const char *name, const String &json, JsonDocument &out, uint32_t timeoutMs = 6000) {
  uint32_t id = sendRequest(name, json);
  uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    size_t len;
    bool failed;
    const uint8_t *msg = ws.poll(len, failed);
    if (failed) return false;
    if (!msg) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    Parsed m;
    if (!parse(msg, len, m)) continue;
    if (m.verb != "REQUEST" && m.requestId == id) {
      out.clear();
      if (m.bodyLen) deserializeJson(out, m.body, m.bodyLen);
      return m.name == "Success";
    }
    dispatch(m);
  }
  return false;
}

String wiimZoneId() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  String id;
  for (const Zone &z : zones) {
    if (hasOutput(z, savedOutput)) id = z.zoneId;
  }
  xSemaphoreGive(mutex);
  return id;
}

bool browse(const char *hierarchy, const char *itemKey, const char *input, bool popAll, const String &zoneId,
            JsonDocument &out) {
  JsonDocument req;
  req["hierarchy"] = hierarchy;
  if (itemKey) req["item_key"] = itemKey;
  if (input) req["input"] = input;
  if (popAll) req["pop_all"] = true;
  if (zoneId.length()) req["zone_or_output_id"] = zoneId;
  String json;
  serializeJson(req, json);
  return call(kBrowse, json, out);
}

bool load(const char *hierarchy, JsonDocument &out) {
  return call(kLoad, String("{\"hierarchy\":\"") + hierarchy + "\",\"offset\":0,\"count\":" + kMaxRows + "}", out);
}

const char *hierarchyFor(List which) { return which == List::kRadio ? "internet_radio" : "playlists"; }

void refreshList(List which) {
  JsonDocument doc(&spiRam);
  const char *h = hierarchyFor(which);
  if (!browse(h, nullptr, nullptr, true, "", doc) || !load(h, doc)) return;
  xSemaphoreTake(mutex, portMAX_DELAY);
  int n = 0;
  for (JsonObject item : doc["items"].as<JsonArray>()) {
    if (n >= kMaxRows) break;
    Row &r = rows[(int)which][n++];
    copyField(r.title, sizeof(r.title), item["title"] | "");
    copyField(r.subtitle, sizeof(r.subtitle), item["subtitle"] | "");
    copyField(r.imageKey, sizeof(r.imageKey), item["image_key"] | "");
  }
  rowCount[(int)which] = n;
  listsVersionCounter++;
  xSemaphoreGive(mutex);
}

// Playlists and tracks hide "Play Now" one or two action lists deep; a station plays on opening.
bool playFrom(const char *hierarchy, String itemKey, const String &zoneId) {
  JsonDocument doc(&spiRam);
  for (int depth = 0; depth < 4; depth++) {
    if (!browse(hierarchy, itemKey.c_str(), nullptr, false, zoneId, doc)) return false;
    String action = doc["action"] | "";
    if (action != "list") return !(doc["is_error"] | false);
    if (!load(hierarchy, doc)) return false;
    JsonArray items = doc["items"];
    String next;
    for (JsonObject item : items) {
      if (strcmp(item["title"] | "", "Play Now") == 0) {
        return browse(hierarchy, item["item_key"] | "", nullptr, false, zoneId, doc);
      }
    }
    for (JsonObject item : items) {
      if (strcmp(item["hint"] | "", "action_list") == 0) {
        next = item["item_key"] | "";
        break;
      }
    }
    if (next.isEmpty()) return false;
    itemKey = next;
  }
  return false;
}

String findTrack(JsonArray items, const char *title) {
  for (JsonObject item : items) {
    if (strcmp(item["hint"] | "", "action_list") == 0 && strcasecmp(item["title"] | "", title) == 0) {
      return item["item_key"] | "";
    }
  }
  return "";
}

void play(List which, int index) {
  String zoneId = wiimZoneId();
  if (zoneId.isEmpty()) {
    Serial.println("[roon] play: the WiiM's zone isn't known yet");
    return;
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool inRange = index >= 0 && index < rowCount[(int)which];
  Row row = {};
  if (inRange) row = rows[(int)which][index];
  xSemaphoreGive(mutex);
  if (!inRange) return;

  JsonDocument doc(&spiRam);
  bool ok = false;
  if (which == List::kHistory) {
    // Roon keeps no history the API can reach, so a past track is found again by search.
    String artist = row.subtitle;
    int comma = artist.indexOf(',');
    if (comma > 0) artist = artist.substring(0, comma);
    String query = String(row.title) + " " + artist;
    if (browse("search", nullptr, query.c_str(), true, zoneId, doc) && load("search", doc)) {
      String key = findTrack(doc["items"], row.title);
      if (key.isEmpty()) {
        for (JsonObject item : doc["items"].as<JsonArray>()) {
          if (strcmp(item["title"] | "", "Tracks") != 0) continue;
          JsonDocument tracks(&spiRam);
          String tracksKey = item["item_key"] | "";
          if (browse("search", tracksKey.c_str(), nullptr, false, zoneId, tracks) && load("search", tracks)) {
            key = findTrack(tracks["items"], row.title);
          }
          break;
        }
      }
      if (key.length()) ok = playFrom("search", key, zoneId);
    }
  } else {
    const char *h = hierarchyFor(which);
    if (browse(h, nullptr, nullptr, true, zoneId, doc) && load(h, doc)) {
      JsonObject item = doc["items"][index];
      if (strcmp(item["title"] | "", row.title) == 0) ok = playFrom(h, item["item_key"] | "", zoneId);
    }
  }
  Serial.printf("[roon] play '%s': %s\n", row.title, ok ? "ok" : "FAILED");
}

void runCommand(const Command &c) {
  switch (c.kind) {
  case Command::kPlay:
    play((List)c.list, c.index);
    break;
  case Command::kControl: {
    String zoneId = wiimZoneId();
    if (zoneId.length()) {
      JsonDocument doc;
      call("com.roonlabs.transport:2/control",
           String("{\"zone_or_output_id\":\"") + zoneId + "\",\"control\":\"" + c.control + "\"}", doc);
    }
    break;
  }
  case Command::kRefresh:
    refreshList(List::kPlaylists);
    refreshList(List::kRadio);
    break;
  }
}

void disconnect() {
  ws.close();
  xSemaphoreTake(mutex, portMAX_DELAY);
  zones.clear();
  xSemaphoreGive(mutex);
  sourceMatched = false;
  versionCounter++;
}

bool connect() {
  link = Link::kConnecting;
  if (!corePort || !ws.connect(coreIp, corePort, "/api")) {
    link = Link::kSearching;
    if (!discover() || !ws.connect(coreIp, corePort, "/api")) return false;
  }
  nextRequestId = 0;
  registerRequestId = zonesRequestId = UINT32_MAX;
  link = Link::kAwaitingApproval; // until Roon answers, which waits on Settings -> Extensions
  sendRegister();
  return true;
}

void taskFn(void *) {
  uint32_t lastAttemptMs = 0;
  uint32_t lastPingMs = 0;
  uint32_t lastListsMs = 0;
  bool attempted = false;
  for (;;) {
    uint32_t now = millis();
    if (historyDirty && now - historyChangedMs > kHistorySaveDelayMs) {
      historyDirty = false;
      saveHistory();
    }
    if (!active) {
      if (ws.connected()) disconnect();
      link = Link::kIdle;
      attempted = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!ws.connected()) {
      if (link == Link::kConnected || link == Link::kAwaitingApproval) disconnect();
      if (attempted && now - lastAttemptMs < kRetryMs) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      attempted = true;
      lastAttemptMs = now;
      if (!connect()) continue;
      lastPingMs = lastListsMs = millis();
    }

    size_t len;
    bool failed;
    while (const uint8_t *msg = ws.poll(len, failed)) {
      Parsed m;
      if (parse(msg, len, m)) dispatch(m);
    }
    if (failed) {
      disconnect();
      continue;
    }
    if (link == Link::kConnected) {
      Command c;
      while (xQueueReceive(commands, &c, 0) == pdTRUE) runCommand(c);
      if (millis() - lastListsMs > kListsRefreshMs) {
        lastListsMs = millis();
        Command refresh = {Command::kRefresh, 0, 0, ""};
        runCommand(refresh);
      }
    }
    now = millis();
    if (now - lastPingMs >= kPingEveryMs) {
      ws.sendPing();
      lastPingMs = now;
    }
    if (now - ws.lastRxMs() > kDeadAfterMs) {
      Serial.println("[roon] connection went quiet, reconnecting");
      disconnect();
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

const char *linkName() {
  switch (link) {
  case Link::kSearching:
    return "searching";
  case Link::kConnecting:
    return "connecting";
  case Link::kAwaitingApproval:
    return "awaiting approval";
  case Link::kConnected:
    return "connected";
  default:
    return "idle";
  }
}

String jsonEscape(const String &s) {
  String out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

} // namespace

void begin(const String &deviceName) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  extensionId = String("com.wiimknob.knob.") + suffix;
  displayName = "WiimKnob (" + deviceName + ")";

  mutex = xSemaphoreCreateMutex();
  commands = xQueueCreate(8, sizeof(Command));
  for (Row *&r : rows) r = (Row *)heap_caps_calloc(kMaxRows, sizeof(Row), MALLOC_CAP_SPIRAM);
  loadState();
  loadHistory();
  xTaskCreatePinnedToCore(taskFn, "roon", 12288, nullptr, 1, nullptr, 0);
}

void setActive(bool on) { active = on; }

bool paired() { return token.length() > 0; }

bool zoneForWiim(const String &wiimTitle, Zone &out) {
  if (wiimTitle.isEmpty()) {
    sourceMatched = false;
    return false;
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  for (const Zone &z : zones) {
    if (hasOutput(z, savedOutput) && z.title.length() && (z.title == wiimTitle || z.prevTitle == wiimTitle)) {
      out = z;
      sourceMatched = true;
      xSemaphoreGive(mutex);
      return true;
    }
  }
  const Zone *match = nullptr;
  int matches = 0;
  for (const Zone &z : zones) {
    if (z.title == wiimTitle) {
      match = &z;
      matches++;
    }
  }
  bool learned = matches == 1 && !hasOutput(*match, savedOutput);
  if (matches == 1) {
    out = *match;
    savedOutput = firstOutput(*match);
    noteHistory(*match);
  }
  sourceMatched = matches == 1;
  xSemaphoreGive(mutex);
  if (learned) {
    saveString("output", savedOutput);
    Serial.printf("[roon] the WiiM is zone '%s'\n", out.name.c_str());
  }
  return matches == 1;
}

bool sourceZone(Zone &out) {
  if (!sourceMatched || link != Link::kConnected) return false;
  bool found = false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  for (const Zone &z : zones) {
    if (hasOutput(z, savedOutput)) {
      out = z;
      found = true;
    }
  }
  xSemaphoreGive(mutex);
  return found;
}

String imageUrl(const String &imageKey, int size) {
  return "http://" + coreIp.toString() + ":" + corePort + "/api/image/" + imageKey + "?scale=fill&width=" +
         size + "&height=" + size + "&format=image/jpeg";
}

uint32_t version() { return versionCounter; }

int copyList(List which, Row *out, int max) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  int n = min(max, rowCount[(int)which]);
  memcpy(out, rows[(int)which], n * sizeof(Row));
  xSemaphoreGive(mutex);
  return n;
}

uint32_t listsVersion() { return listsVersionCounter; }

void requestPlay(List which, int index) {
  Command c = {Command::kPlay, (uint8_t)which, (int16_t)index, ""};
  xQueueSend(commands, &c, 0);
}

void requestControl(const char *control) {
  Command c = {Command::kControl, 0, 0, ""};
  strlcpy(c.control, control, sizeof(c.control));
  xQueueSend(commands, &c, 0);
}

void requestRefreshLists() {
  Command c = {Command::kRefresh, 0, 0, ""};
  xQueueSend(commands, &c, 0);
}

String statusJson() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  String zone;
  for (const Zone &z : zones) {
    if (hasOutput(z, savedOutput)) zone = z.name;
  }
  String json = String("{\"link\":\"") + linkName() + "\",\"extension\":\"" + jsonEscape(displayName) +
                "\",\"core\":\"" + jsonEscape(coreName) + "\",\"host\":\"" +
                (corePort ? coreIp.toString() + ":" + corePort : String("")) + "\",\"zones\":" + zones.size() +
                ",\"wiim_zone\":\"" + jsonEscape(zone) + "\",\"roon_is_source\":" +
                (sourceMatched ? "true" : "false") + ",\"history\":" + rowCount[(int)List::kHistory] +
                ",\"playlists\":" + rowCount[(int)List::kPlaylists] + ",\"radio\":" +
                rowCount[(int)List::kRadio] + "}";
  xSemaphoreGive(mutex);
  return json;
}

} // namespace roon
