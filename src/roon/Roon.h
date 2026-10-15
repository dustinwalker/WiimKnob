#pragma once

#include <Arduino.h>

// Track info and covers from a Roon Core when it's the WiiM's source; volume stays with the WiiM.
namespace roon {

struct Zone {
  String zoneId;
  String outputIds; // comma-separated: a grouped zone has several
  String name;
  String state;     // playing, paused, loading, stopped
  String title;
  String artist;
  String album;
  String imageKey;
  String prevTitle; // the WiiM can still report this one for a poll after Roon moves on
};

// Call once Wi-Fi is up.
void begin();
// Closes the connection while the screen is off; reconnecting takes well under a second.
void setActive(bool active);

// Learns the WiiM's zone by matching its title once, then remembers the output: no setup needed.
bool zoneForWiim(const String &wiimTitle, Zone &out);
String imageUrl(const String &imageKey, int size);

// Bumped on every track or cover change in any zone.
uint32_t version();

String statusJson();

} // namespace roon
