#include "NetConfig.h"
#include "Config.h"

#include <Preferences.h>

namespace net {

namespace {
constexpr const char *kNamespace = "net";
}

Config load() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  Config c;
  c.ssid = prefs.getString("ssid", WIFI_SSID);
  c.password = prefs.getString("pass", WIFI_PASSWORD);
  c.wiimHost = prefs.getString("wiim", WIIM_HOST);
  c.wiimName = prefs.getString("wiimname", "");
  prefs.end();
  return c;
}

void save(const Config &c) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putString("ssid", c.ssid);
  prefs.putString("pass", c.password);
  prefs.putString("wiim", c.wiimHost);
  prefs.putString("wiimname", c.wiimName);
  prefs.end();
}

void requestSetup() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putBool("setup", true);
  prefs.end();
}

bool takeSetupRequest() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  bool requested = prefs.getBool("setup", false);
  if (requested) prefs.remove("setup");
  prefs.end();
  return requested;
}

} // namespace net
