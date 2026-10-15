#include "Roon.h"
#include "WsClient.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>

namespace roon {

namespace {

constexpr const char *kServiceId = "00720724-5143-4a9b-abac-0e50cba674bb";
constexpr uint16_t kSoodPort = 9003;
constexpr uint32_t kPingEveryMs = 10000;
constexpr uint32_t kDeadAfterMs = 35000;
constexpr uint32_t kRetryMs = 5000;

enum class Link { kIdle, kSearching, kConnecting, kAwaitingApproval, kConnected };

struct SpiRamAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} spiRam;

SemaphoreHandle_t mutex;
WsClient ws;
volatile bool active = true;
Link link = Link::kIdle;
IPAddress coreIp;
uint16_t corePort = 0;
String coreName;
String token;
String savedOutput;
std::vector<Zone> zones;
volatile uint32_t versionCounter = 0;
uint32_t nextRequestId = 0;
uint32_t registerRequestId = UINT32_MAX;
uint32_t zonesRequestId = UINT32_MAX;

void loadState() {
  Preferences prefs;
  prefs.begin("roon", true);
  token = prefs.getString("token", "");
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
  doc["extension_id"] = "com.wiimknob.knob";
  doc["display_name"] = "WiimKnob";
  doc["display_version"] = "1.0";
  doc["publisher"] = "WiimKnob";
  doc["email"] = "";
  doc["website"] = "https://github.com/dustinwalker/WiimKnob";
  doc["required_services"].add("com.roonlabs.transport:2");
  doc["required_services"].add("com.roonlabs.image:1");
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
  if (title != z->title || imageKey != z->imageKey) {
    if (title != z->title) z->prevTitle = z->title;
    versionCounter++;
  }
  z->title = title;
  z->artist = np["three_line"]["line2"] | "";
  z->album = np["three_line"]["line3"] | "";
  z->imageKey = imageKey;
}

void handleZones(const char *name, const uint8_t *body, size_t len) {
  // Seek updates arrive every second and only the track fields matter here.
  static JsonDocument filter;
  if (filter.isNull()) {
    JsonObject zone = filter["zones"][0].to<JsonObject>();
    zone["zone_id"] = true;
    zone["display_name"] = true;
    zone["state"] = true;
    zone["outputs"][0]["output_id"] = true;
    zone["now_playing"]["image_key"] = true;
    zone["now_playing"]["three_line"] = true;
    filter["zones_added"][0] = zone;
    filter["zones_changed"][0] = zone;
    filter["zones_removed"] = true;
  }
  JsonDocument doc(&spiRam);
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter))) return;

  xSemaphoreTake(mutex, portMAX_DELAY);
  if (strcmp(name, "Subscribed") == 0) zones.clear();
  for (const char *key : {"zones", "zones_added", "zones_changed"}) {
    for (JsonObject z : doc[key].as<JsonArray>()) applyZone(z);
  }
  for (const char *id : doc["zones_removed"].as<JsonArray>()) {
    for (size_t i = 0; i < zones.size(); i++) {
      if (zones[i].zoneId == id) zones.erase(zones.begin() + i--);
    }
  }
  xSemaphoreGive(mutex);
}

void handleMessage(const uint8_t *msg, size_t len) {
  const uint8_t *end = msg + len;
  const uint8_t *eol = (const uint8_t *)memchr(msg, '\n', len);
  if (!eol || len < 7 || memcmp(msg, "MOO/1 ", 6) != 0) return;
  String first((const char *)msg + 6, eol - msg - 6);
  int space = first.indexOf(' ');
  String verb = first.substring(0, space);
  String name = first.substring(space + 1);

  String requestId;
  size_t contentLength = 0;
  const uint8_t *p = eol + 1;
  while (p < end) {
    const uint8_t *lineEnd = (const uint8_t *)memchr(p, '\n', end - p);
    if (!lineEnd) return;
    if (lineEnd == p) {
      p++;
      break;
    }
    String line((const char *)p, lineEnd - p);
    if (line.startsWith("Request-Id: ")) requestId = line.substring(12);
    else if (line.startsWith("Content-Length: ")) contentLength = line.substring(16).toInt();
    p = lineEnd + 1;
  }
  const uint8_t *body = p;
  size_t bodyLen = min(contentLength, (size_t)(end - p));

  if (verb == "REQUEST") {
    sendComplete(requestId, name == "com.roonlabs.ping:1/ping" ? "Success" : "InvalidRequest");
    return;
  }
  uint32_t id = requestId.toInt();
  if (id == registerRequestId) {
    if (name != "Registered") {
      Serial.printf("[roon] register: %s\n", name.c_str());
      return;
    }
    JsonDocument doc(&spiRam);
    if (deserializeJson(doc, body, bodyLen)) return;
    String newToken = doc["token"] | "";
    if (newToken.length() && newToken != token) {
      token = newToken;
      saveString("token", token);
    }
    coreName = doc["display_name"] | "Roon";
    link = Link::kConnected;
    Serial.printf("[roon] registered with %s\n", coreName.c_str());
    zonesRequestId = sendRequest("com.roonlabs.transport:2/subscribe_zones", "{\"subscription_key\":0}");
  } else if (id == zonesRequestId) {
    handleZones(name.c_str(), body, bodyLen);
  }
}

void disconnect() {
  ws.close();
  xSemaphoreTake(mutex, portMAX_DELAY);
  zones.clear();
  xSemaphoreGive(mutex);
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
  bool attempted = false;
  for (;;) {
    if (!active) {
      if (ws.connected()) disconnect();
      link = Link::kIdle;
      attempted = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    uint32_t now = millis();
    if (!ws.connected()) {
      if (link == Link::kConnected || link == Link::kAwaitingApproval) disconnect();
      if (attempted && now - lastAttemptMs < kRetryMs) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      attempted = true;
      lastAttemptMs = now;
      if (!connect()) continue;
      lastPingMs = millis();
    }

    size_t len;
    bool failed;
    while (const uint8_t *msg = ws.poll(len, failed)) handleMessage(msg, len);
    if (failed) {
      disconnect();
      continue;
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

bool hasOutput(const Zone &z, const String &outputId) {
  if (outputId.isEmpty()) return false;
  return ("," + z.outputIds + ",").indexOf("," + outputId + ",") >= 0;
}

String firstOutput(const Zone &z) {
  int comma = z.outputIds.indexOf(',');
  return comma < 0 ? z.outputIds : z.outputIds.substring(0, comma);
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

void begin() {
  mutex = xSemaphoreCreateMutex();
  loadState();
  xTaskCreatePinnedToCore(taskFn, "roon", 8192, nullptr, 1, nullptr, 0);
}

void setActive(bool on) { active = on; }

bool zoneForWiim(const String &wiimTitle, Zone &out) {
  if (wiimTitle.isEmpty()) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  for (const Zone &z : zones) {
    if (hasOutput(z, savedOutput) && z.title.length() && (z.title == wiimTitle || z.prevTitle == wiimTitle)) {
      out = z;
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
  }
  xSemaphoreGive(mutex);
  if (learned) {
    saveString("output", savedOutput);
    Serial.printf("[roon] the WiiM is zone '%s'\n", out.name.c_str());
  }
  return matches == 1;
}

String imageUrl(const String &imageKey, int size) {
  return "http://" + coreIp.toString() + ":" + corePort + "/api/image/" + imageKey + "?scale=fit&width=" +
         size + "&height=" + size + "&format=image/jpeg";
}

uint32_t version() { return versionCounter; }

String statusJson() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  String zone;
  for (const Zone &z : zones) {
    if (hasOutput(z, savedOutput)) zone = z.name;
  }
  String json = String("{\"link\":\"") + linkName() + "\",\"core\":\"" + jsonEscape(coreName) + "\",\"host\":\"" +
                (corePort ? coreIp.toString() + ":" + corePort : String("")) + "\",\"zones\":" + zones.size() +
                ",\"wiim_zone\":\"" + jsonEscape(zone) + "\"}";
  xSemaphoreGive(mutex);
  return json;
}

} // namespace roon
