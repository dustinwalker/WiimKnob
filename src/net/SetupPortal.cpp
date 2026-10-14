#include "SetupPortal.h"
#include "ui/Ui.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

namespace net {
namespace portal {

namespace {

enum class State { kIdle, kConnecting, kDiscovering, kChooseWiim, kFailed, kDone };

const IPAddress kApIp(192, 168, 4, 1);
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kIdleRestartMs = 10 * 60 * 1000; // retry the saved network if nobody shows up

WebServer server(80);
DNSServer dns;
Config config;
State state = State::kIdle;
uint32_t stateSinceMs = 0;
uint32_t lastRequestMs = 0;
String scanJson = "[]";
String wiimJson = "[]";

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiimKnob setup</title>
<style>
body{font-family:-apple-system,system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px 16px;max-width:480px}
h1{font-size:22px;margin:0 0 4px}p{color:#aaa;margin:4px 0 20px}
label{display:block;margin:14px 0 6px;color:#ccc}
select,input,button{width:100%;box-sizing:border-box;font-size:17px;padding:12px;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee}
button{background:#e53935;border:0;font-weight:600;margin-top:20px}
button.alt{background:#2a2a2a}
#status{margin-top:20px;color:#ccc;min-height:1.4em}
.hidden{display:none}
</style></head><body>
<h1>WiimKnob setup</h1>
<p>Pick the Wi-Fi network the knob should join, then the WiiM it controls.</p>
<div id="wifi">
<label for="ssid">Network</label>
<select id="ssid"><option>Scanning...</option></select>
<label for="pass">Password</label>
<input id="pass" type="password" autocomplete="current-password">
<button onclick="connect(false)">Connect</button>
<button class="alt" id="keep" onclick="connect(true)">Keep current Wi-Fi</button>
</div>
<div id="wiim" class="hidden">
<label for="host">WiiM to control</label>
<select id="host"></select>
<button onclick="finish()">Save and restart</button>
</div>
<div id="status"></div>
<script>
const $=id=>document.getElementById(id);
let current='';
async function scan(){
  const r=await fetch('/scan');const d=await r.json();current=d.current;
  $('keep').textContent='Keep current Wi-Fi ('+current+')';
  const s=$('ssid');s.innerHTML='';
  if(!d.networks.length){s.innerHTML='<option>No networks found</option>';setTimeout(scan,3000);return;}
  for(const n of d.networks){const o=document.createElement('option');o.value=n.ssid;
    o.textContent=n.ssid+(n.open?'':' \u{1F512}')+'  ('+n.rssi+' dBm)';if(n.ssid===current)o.selected=true;s.appendChild(o);}
}
async function connect(keep){
  const b=new URLSearchParams();if(!keep){b.set('ssid',$('ssid').value);b.set('pass',$('pass').value);}
  $('status').textContent='Connecting... if your phone drops off WiimKnob-Setup, rejoin it.';
  await fetch('/connect',{method:'POST',body:b});poll();
}
async function poll(){
  let d;try{d=await (await fetch('/state')).json();}catch(e){setTimeout(poll,1500);return;}
  if(d.state==='connecting'||d.state==='discovering'){$('status').textContent=d.state==='connecting'?'Connecting...':'Looking for WiiMs...';setTimeout(poll,1000);return;}
  if(d.state==='failed'){$('status').textContent="Couldn't join that network. Check the password and try again.";return;}
  if(d.state==='choose'){$('wifi').classList.add('hidden');$('wiim').classList.remove('hidden');
    const h=$('host');h.innerHTML='';
    for(const w of d.wiims){const o=document.createElement('option');o.value=w.host;o.dataset.name=w.name;o.textContent=w.name+' ('+w.host+')';if(w.current)o.selected=true;h.appendChild(o);}
    $('status').textContent=d.wiims.length?'':'No WiiMs found on this network.';}
}
async function finish(){
  const o=$('host').selectedOptions[0];const b=new URLSearchParams({host:o.value,name:o.dataset.name});
  await fetch('/finish',{method:'POST',body:b});
  $('wiim').classList.add('hidden');$('status').textContent='Saved. The knob is restarting; you can close this page.';
}
scan();
</script></body></html>)HTML";

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 4);
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

void setState(State s, const char *status) {
  state = s;
  stateSinceMs = millis();
  if (status) ui::setSetupStatus(status);
}

void collectScan() {
  int n = WiFi.scanComplete();
  if (n < 0) return;
  String json = "[";
  int added = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) dup |= WiFi.SSID(j) == ssid; // one entry per network, strongest first
    if (dup) continue;
    if (added++) json += ',';
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
            ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  scanJson = json;
  WiFi.scanDelete();
}

void discoverWiims() {
  MDNS.begin("wiimknob-setup");
  int n = MDNS.queryService("linkplay", "tcp");
  String json = "[";
  bool haveCurrent = false;
  for (int i = 0; i < n; i++) {
    String host = MDNS.address(i).toString();
    String name = MDNS.instanceName(i);
    if (name.isEmpty()) name = MDNS.hostname(i);
    bool current = host == config.wiimHost;
    haveCurrent |= current;
    if (i) json += ',';
    json += "{\"host\":\"" + host + "\",\"name\":\"" + jsonEscape(name) + "\",\"current\":" +
            (current ? "true" : "false") + "}";
  }
  if (!haveCurrent && config.wiimHost.length()) {
    if (n > 0) json += ',';
    String name = config.wiimName.length() ? config.wiimName : String("Current WiiM");
    json += "{\"host\":\"" + config.wiimHost + "\",\"name\":\"" + jsonEscape(name) + "\",\"current\":true}";
  }
  json += "]";
  wiimJson = json;
}

const char *stateName() {
  switch (state) {
  case State::kConnecting:
    return "connecting";
  case State::kDiscovering:
    return "discovering";
  case State::kChooseWiim:
    return "choose";
  case State::kFailed:
    return "failed";
  case State::kDone:
    return "done";
  default:
    return "idle";
  }
}

void restart() {
  delay(1500);
  ESP.restart();
}

void onCancel() { ESP.restart(); }

void setupRoutes() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", kPage); });
  server.on("/scan", HTTP_GET, []() {
    collectScan();
    if (WiFi.scanComplete() == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
    server.send(200, "application/json",
                "{\"current\":\"" + jsonEscape(config.ssid) + "\",\"networks\":" + scanJson + "}");
  });
  server.on("/connect", HTTP_POST, []() {
    if (server.hasArg("ssid")) {
      config.ssid = server.arg("ssid");
      config.password = server.arg("pass");
    }
    server.send(200, "text/plain", "ok");
    WiFi.disconnect();
    WiFi.begin(config.ssid.c_str(), config.password.c_str());
    setState(State::kConnecting, ("Joining " + config.ssid + "...").c_str());
  });
  server.on("/state", HTTP_GET, []() {
    String body = "{\"state\":\"" + String(stateName()) + "\"";
    if (state == State::kChooseWiim) body += ",\"wiims\":" + wiimJson;
    server.send(200, "application/json", body + "}");
  });
  server.on("/finish", HTTP_POST, []() {
    config.wiimHost = server.arg("host");
    config.wiimName = server.arg("name");
    save(config);
    server.send(200, "text/plain", "ok");
    setState(State::kDone, "Saved. Restarting...");
  });
  // Every unknown URL (the phone's captive-portal probes included) lands on the setup page.
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
}

} // namespace

void run(const Config &current, void (*tick)()) {
  config = current;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(kApName);
  WiFi.scanNetworks(true);
  dns.start(53, "*", kApIp);
  setupRoutes();
  server.begin();

  ui::showSetup(kApName, "WIFI:T:nopass;S:WiimKnob-Setup;;", onCancel);
  setState(State::kIdle, "Waiting for your phone...");
  Serial.printf("[setup] join '%s' and open http://192.168.4.1\n", kApName);

  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    tick();

    uint32_t now = millis();
    if (WiFi.softAPgetStationNum() > 0) lastRequestMs = now;
    if (state == State::kConnecting) {
      if (WiFi.status() == WL_CONNECTED) {
        setState(State::kDiscovering, ("Joined " + config.ssid + ". Looking for WiiMs...").c_str());
      } else if (now - stateSinceMs > kConnectTimeoutMs) {
        WiFi.disconnect();
        setState(State::kFailed, "Couldn't join that network. Try again on your phone.");
      }
    } else if (state == State::kDiscovering) {
      discoverWiims();
      setState(State::kChooseWiim, "Pick your WiiM on your phone.");
    } else if (state == State::kDone && now - stateSinceMs > 500) {
      restart();
    } else if (state == State::kIdle && now - lastRequestMs > kIdleRestartMs) {
      ESP.restart();
    }
    delay(5);
  }
}

} // namespace portal
} // namespace net
