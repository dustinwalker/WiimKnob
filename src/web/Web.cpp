#include "Web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <mdns.h>

#include "net/NetConfig.h"
#include "power/Power.h"
#include "roon/Roon.h"
#include "wiim/WiimTask.h"

namespace web {

namespace {

String wiimLabel;
void (*apply)(settings::Item) = nullptr;

SemaphoreHandle_t peersMutex;
String peersJson = "[]";
uint32_t peersAtMs = 0;
volatile bool discovering = false;
constexpr uint32_t kPeersMaxAgeMs = 60000;

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiimKnob</title>
<style>
  :root { --bg: #050b14; --card: #0a1420; --fg: #fff; --muted: #8fa3b3; --line: #172636; --accent: #d6003f; --arrow: #ff295e; }
  * { box-sizing: border-box; }
  body { margin: 0; min-height: 100vh; color: var(--fg); font: 16px/1.5 system-ui, -apple-system, sans-serif;
         background: radial-gradient(ellipse 80% 50% at 50% 0, #0d2233 0, var(--bg) 70%) fixed, var(--bg); }
  main { max-width: 480px; margin: 0 auto; padding: 32px 16px 56px; }
  h1 { font: 700 28px/1.2 Merriweather, Georgia, Cambria, "Times New Roman", serif; margin: 0 0 4px; }
  .sub { color: var(--muted); font-size: 14px; margin: 0 0 24px; }
  section { background: var(--card); border: 1px solid var(--line); border-radius: 2px; padding: 18px; margin-bottom: 16px; }
  h2 { font: 700 15px/1.2 Merriweather, Georgia, serif; color: var(--fg); margin: 0 0 14px; }
  h2::before { content: "\2192"; color: var(--arrow); margin-right: 8px; }
  .now { display: flex; gap: 14px; align-items: center; }
  .now img { width: 64px; height: 64px; border-radius: 2px; object-fit: cover; background: var(--line); flex: none; }
  .now .line { font-weight: 600; }
  .now .meta { color: var(--muted); font-size: 14px; }
  label { display: block; font-weight: 600; margin-bottom: 6px; }
  select, input[type=range] { width: 100%; }
  select { font: inherit; padding: 10px; border-radius: 2px; border: 1px solid var(--line); background: var(--bg); color: var(--fg); }
  select:focus, input:focus, button:focus-visible, a:focus-visible { outline: 2px solid var(--arrow); outline-offset: 1px; }
  input[type=range], input[type=checkbox] { accent-color: var(--accent); }
  .field + .field { margin-top: 14px; }
  label.check { display: flex; gap: 8px; align-items: center; font-weight: 400; }
  .row { display: flex; justify-content: space-between; align-items: baseline; }
  .hint { color: var(--muted); font-size: 13px; margin: 8px 0 0; }
  .saved { color: var(--arrow); font-size: 13px; min-height: 1em; }
  dl { display: grid; grid-template-columns: auto 1fr; gap: 4px 12px; margin: 0; font-size: 14px; }
  dt { color: var(--muted); }
  dd { margin: 0; overflow-wrap: anywhere; }
  .knobs { list-style: none; margin: 0; padding: 0; }
  .knobs li + li { margin-top: 8px; }
  .knobs a { display: flex; justify-content: space-between; gap: 12px; padding: 10px; border: 1px solid var(--line);
             border-radius: 2px; color: var(--fg); text-decoration: none; }
  .knobs a:hover { border-color: var(--arrow); }
  .knobs a.self { border-color: var(--accent); }
  .knobs .meta { color: var(--muted); font-size: 14px; }
</style>
</head>
<body>
<main>
  <h1>WiimKnob</h1>
  <p class="sub" id="status">Loading…</p>

  <section id="knobsCard" hidden>
    <h2>Devices</h2>
    <ul class="knobs" id="knobs"></ul>
  </section>

  <section>
    <h2>Now playing</h2>
    <div class="now">
      <img id="cover" alt="">
      <div><div class="line" id="line">Nothing playing</div><div class="meta" id="source"></div></div>
    </div>
  </section>

  <section>
    <h2>Screen</h2>
    <div id="screen"></div>
  </section>

  <section>
    <h2>Pages</h2>
    <div id="pages"></div>
  </section>

  <section>
    <h2>Roon</h2>
    <div class="field">
      <label for="zone">Zone</label>
      <select id="zone"></select>
      <p class="hint" id="zoneHint"></p>
    </div>
  </section>

  <section>
    <h2>Battery</h2>
    <dl>
      <dt>Charge</dt><dd id="charge"></dd>
      <dt>Power</dt><dd id="power"></dd>
    </dl>
    <div id="battery" style="margin-top:14px"></div>
  </section>

  <p class="saved" id="saved"></p>

  <section>
    <h2>Device</h2>
    <dl>
      <dt>WiiM</dt><dd id="wiim"></dd>
      <dt>Address</dt><dd id="ip"></dd>
      <dt>WiFi signal</dt><dd id="rssi"></dd>
      <dt>Firmware</dt><dd id="fw"></dd>
      <dt>Uptime</dt><dd id="uptime"></dd>
      <dt>Free memory</dt><dd id="heap"></dd>
    </dl>
  </section>
</main>
<script>
const $ = id => document.getElementById(id);
const GROUP = { bright: "screen", ctrl: "screen", screen: "screen", sleep: "screen", recent: "pages", tunein: "pages", batcal: "battery" };
const HINT = { sleep: "On battery the knob, and this page, go to sleep after this long untouched. On USB it stays awake.",
               batcal: "Keeps the screen on and logs the discharge to learn this battery's curve. Run it from a full charge." };
let editing = false, built = false, lastArt = "", self = "", knobTries = 0;

function uptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + "d " : "") + (d || h ? h + "h " : "") + m + "m";
}

async function save(body) {
  let ok = false;
  try { ok = (await fetch("/api/settings", { method: "POST", headers: { "Content-Type": "application/json" },
                                              body: JSON.stringify(body) })).ok; } catch {}
  $("saved").textContent = ok ? "Saved" : "Couldn’t save";
  setTimeout(() => $("saved").textContent = "", 1500);
  refresh();
}

function guard(el) {
  el.addEventListener("focus", () => editing = true);
  el.addEventListener("blur", () => editing = false);
}

function build(settings) {
  for (const s of settings) {
    const box = $(GROUP[s.key]);
    if (!box) continue;
    const f = document.createElement("div");
    f.className = "field";
    if (s.toggle) {
      f.innerHTML = `<label class="check"><input type="checkbox" id="s_${s.key}"> ${s.label}</label>`;
      box.append(f);
      $("s_" + s.key).addEventListener("change", e => save({ [s.key]: e.target.checked ? 1 : 0 }));
    } else if (s.key === "bright") {
      f.innerHTML = `<div class="row"><label for="s_bright">${s.label}</label><span id="v_bright"></span></div>
                     <input type="range" id="s_bright" min="0" max="${s.options.length - 1}" step="1">`;
      box.append(f);
      const r = $("s_bright");
      r.addEventListener("input", () => { editing = true; $("v_bright").textContent = s.options[r.value][1]; });
      r.addEventListener("change", () => { editing = false; save({ bright: s.options[r.value][0] }); });
    } else {
      f.innerHTML = `<label for="s_${s.key}">${s.label}</label><select id="s_${s.key}"></select>`;
      box.append(f);
      const sel = $("s_" + s.key);
      for (const [v, t] of s.options) sel.add(new Option(t, v));
      guard(sel);
      sel.addEventListener("change", e => { editing = false; save({ [s.key]: +e.target.value }); });
    }
    if (HINT[s.key]) f.insertAdjacentHTML("beforeend", `<p class="hint">${HINT[s.key]}</p>`);
  }
  built = true;
}

async function refresh() {
  let s;
  try { s = await (await fetch("/api/state")).json(); } catch { $("status").textContent = "Knob not responding (asleep?)"; return; }
  self = s.name;
  document.title = "WiimKnob " + s.name.slice(9);
  $("status").textContent = s.wiim + " · " + s.name;
  $("wiim").textContent = s.wiim;
  if (!built) build(s.settings);
  if (!editing) {
    for (const st of s.settings) {
      const el = $("s_" + st.key);
      if (!el) continue;
      if (st.toggle) el.checked = !!st.value;
      else if (st.key === "bright") { el.value = st.options.findIndex(o => o[0] === st.value); $("v_bright").textContent = st.text; }
      else el.value = st.value;
    }
    const z = $("zone");
    z.innerHTML = "";
    s.zone.choices.forEach((name, i) => z.add(new Option(name, i, false, i === s.zone.current)));
  }

  const np = s.now;
  const playing = np.title || np.artist;
  $("line").textContent = playing ? np.title || "—" : "Nothing playing";
  $("source").textContent = playing ? [np.artist, np.source + " · " + np.status].filter(Boolean).join(" · ") : np.source;
  if (np.art !== lastArt) { lastArt = np.art; np.art ? $("cover").src = np.art : $("cover").removeAttribute("src"); }
}

async function refreshDevice() {
  let d;
  try { d = await (await fetch("/status")).json(); } catch { return; }
  $("charge").textContent = d.percent + "% · " + d.voltage.toFixed(2) + " V";
  $("power").textContent = d.usb_power ? "USB" : "Battery";
  $("ip").textContent = location.host + " · " + d.hostname + ".local";
  $("rssi").textContent = d.rssi + " dBm";
  $("fw").textContent = d.firmware;
  $("uptime").textContent = uptime(d.uptime_s);
  $("heap").textContent = Math.round(d.heap_internal / 1024) + " KB internal";
  const r = d.roon;
  $("zoneHint").textContent = r.link === "awaiting approval" ? `Enable “${r.extension}” in Roon › Settings › Extensions.`
    : r.link !== "connected" ? "Roon: " + r.link
    : $("zone").value === "0" ? (r.wiim_zone ? "Automatic is following " + r.wiim_zone + "." : "Automatic learns the zone from what the WiiM plays.") : "";
}

async function refreshKnobs() {
  let k;
  try { k = await (await fetch("/api/knobs")).json(); } catch { return; }
  if (k.length < 2) { if (++knobTries < 4) setTimeout(refreshKnobs, 3000); return; }
  $("knobsCard").hidden = false;
  $("knobs").innerHTML = "";
  const rank = n => n.name === self ? 0 : n.name.startsWith("wiimknob-") ? 1 : 2;
  k.sort((a, b) => rank(a) - rank(b) || a.name.localeCompare(b.name));
  for (const n of k) {
    const li = document.createElement("li");
    const a = document.createElement("a");
    a.href = "http://" + n.ip + "/";
    if (n.name === self) a.className = "self";
    a.innerHTML = `<span></span><span class="meta"></span>`;
    a.children[0].textContent = n.wiim || n.name;
    a.children[1].textContent = n.name === self ? "this knob" : n.name;
    li.append(a);
    $("knobs").append(li);
  }
}

guard($("zone"));
$("zone").addEventListener("change", e => { editing = false; save({ zone: +e.target.value }); });

refresh().then(() => { refreshDevice(); refreshKnobs(); });
setInterval(refresh, 3000);
setInterval(refreshDevice, 15000);
setInterval(refreshKnobs, 30000);
</script>
</body>
</html>
)HTML";

void discoverTask(void *) {
  mdns_result_t *results = nullptr;
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  JsonObject me = list.add<JsonObject>();
  me["name"] = net::hostname();
  me["wiim"] = wiimLabel;
  me["ip"] = WiFi.localIP().toString();
  if (mdns_query_ptr("_arduino", "_tcp", 2000, 16, &results) == ESP_OK) {
    for (mdns_result_t *r = results; r; r = r->next) {
      if (!r->hostname || !r->addr || !strcmp(r->hostname, net::hostname())) continue;
      bool knob = !strncmp(r->hostname, "wiimknob-", 9);
      if (!knob && strcmp(r->hostname, "roon-visualizer")) continue;
      mdns_ip_addr_t *a = r->addr;
      while (a && a->addr.type != ESP_IPADDR_TYPE_V4) a = a->next;
      if (!a) continue;
      char ip[16];
      snprintf(ip, sizeof(ip), IPSTR, IP2STR(&a->addr.u_addr.ip4));
      bool dup = false;
      for (JsonObject o : list) dup |= o["name"] == r->hostname; // one answer per interface
      if (dup) continue;
      JsonObject o = list.add<JsonObject>();
      o["name"] = r->hostname;
      o["ip"] = ip;
      o["wiim"] = knob ? "" : "Roon Visualizer";
      for (size_t i = 0; i < r->txt_count; i++) {
        if (!strcmp(r->txt[i].key, "wiim") && r->txt[i].value) o["wiim"] = r->txt[i].value;
      }
    }
    mdns_query_results_free(results);
  }
  String json;
  serializeJson(doc, json);
  xSemaphoreTake(peersMutex, portMAX_DELAY);
  peersJson = json;
  xSemaphoreGive(peersMutex);
  peersAtMs = millis() | 1;
  discovering = false;
  vTaskDelete(nullptr);
}

void handleKnobs() {
  // The query blocks for its whole timeout, so it runs off loop() and the page gets the last answer.
  if (!discovering && (!peersAtMs || millis() - peersAtMs > kPeersMaxAgeMs)) {
    discovering = true;
    if (xTaskCreate(discoverTask, "mdns-peers", 6144, nullptr, 1, nullptr) != pdPASS) discovering = false;
  }
  xSemaphoreTake(peersMutex, portMAX_DELAY);
  String json = peersJson;
  xSemaphoreGive(peersMutex);
  server().send(200, "application/json", json);
}

void handleState() {
  JsonDocument doc;
  doc["name"] = net::hostname();
  doc["wiim"] = wiimLabel;

  wiim::PlayerState st = wiim::task::getPlayerState();
  wiim::TrackMetadata meta = wiim::task::getMetadata();
  roon::Zone zone;
  bool fromRoon = roon::sourceZone(zone);
  JsonObject now = doc["now"].to<JsonObject>();
  now["title"] = meta.title;
  now["artist"] = meta.artist;
  now["art"] = meta.albumArtUrl;
  now["status"] = fromRoon ? zone.state : st.status == "play" ? String("playing") : st.status;
  now["source"] = fromRoon ? "Roon · " + zone.name : String("WiiM");

  JsonArray list = doc["settings"].to<JsonArray>();
  char text[16];
  for (int i = 0; i < settings::kCount; i++) {
    settings::Item item = (settings::Item)i;
    if (item == settings::kRoonZone) continue;
    JsonObject s = list.add<JsonObject>();
    s["key"] = settings::key(item);
    s["label"] = settings::label(item);
    s["value"] = settings::value(item);
    s["toggle"] = settings::isToggle(item);
    settings::format(item, text, sizeof(text));
    s["text"] = text;
    JsonArray opts = s["options"].to<JsonArray>();
    for (int j = 0; j < settings::optionCount(item); j++) {
      int v = settings::option(item, j);
      settings::formatValue(item, v, text, sizeof(text));
      JsonArray o = opts.add<JsonArray>();
      o.add(v);
      o.add(text);
    }
  }

  int current;
  std::vector<String> choices = roon::zoneChoices(current);
  doc["zone"]["current"] = current;
  JsonArray names = doc["zone"]["choices"].to<JsonArray>();
  for (const String &n : choices) names.add(n);

  String body;
  serializeJson(doc, body);
  server().send(200, "application/json", body);
}

void handleSettings() {
  JsonDocument doc;
  if (deserializeJson(doc, server().arg("plain")) || !doc.is<JsonObject>()) {
    server().send(400, "text/plain", "bad json");
    return;
  }
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (kv.key() == "zone") {
      roon::setZoneChoice(kv.value().as<int>());
      apply(settings::kRoonZone);
      continue;
    }
    for (int i = 0; i < settings::kCount; i++) {
      settings::Item item = (settings::Item)i;
      if (item == settings::kRoonZone || kv.key() != settings::key(item)) continue;
      settings::set(item, kv.value().as<int>());
      apply(item);
    }
  }
  power::noteActivity(); // a change counts as use, but polling doesn't, or an open tab would keep the knob lit
  server().send(200, "application/json", "{\"ok\":true}");
}

} // namespace

WebServer &server() {
  static WebServer s(80);
  return s;
}

void begin(const String &wiimName, void (*applySetting)(settings::Item)) {
  wiimLabel = wiimName;
  apply = applySetting;
  peersMutex = xSemaphoreCreateMutex();
  MDNS.addServiceTxt("arduino", "tcp", "wiim", wiimName.c_str()); // ArduinoOTA's service, so other knobs can label this one
  server().on("/", HTTP_GET, [] { server().send_P(200, "text/html", kPage); });
  server().on("/api/state", HTTP_GET, handleState);
  server().on("/api/settings", HTTP_POST, handleSettings);
  server().on("/api/knobs", HTTP_GET, handleKnobs);
  server().begin();
}

void service() { server().handleClient(); }

} // namespace web
