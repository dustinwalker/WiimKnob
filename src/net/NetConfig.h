#pragma once

#include <Arduino.h>

// Wi-Fi credentials and the WiiM's address, saved by the phone setup page (see SetupPortal.h).
// Until something has been saved, falls back to WIFI_SSID / WIFI_PASSWORD / WIIM_HOST in
// Config.h.
namespace net {

struct Config {
  String ssid;
  String password;
  String wiimHost;
  String wiimName; // as the WiiM advertises itself, for display; empty if never picked
};

Config load();
void save(const Config &config);

// Makes the next boot start the setup portal instead of connecting normally.
void requestSetup();
// Reads and clears that request.
bool takeSetupRequest();

} // namespace net
