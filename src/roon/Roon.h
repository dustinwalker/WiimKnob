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
  long seekS = -1;
  long lengthS = 0;
};

// One row of the Library page. Fixed-size so a hundred of them can live in PSRAM.
struct Row {
  char title[96];
  char subtitle[80];
  char imageKey[48];
};

enum class List { kHistory, kPlaylists, kRadio };
constexpr int kListCount = 3;
constexpr int kMaxRows = 100;

// Call once Wi-Fi is up. deviceName tells knobs apart in Roon's Extensions list.
void begin(const String &deviceName);
// Off closes the connection: the screen is dark on battery.
void setActive(bool active);
bool paired(); // approved in Roon at least once

// Learns the WiiM's zone by matching its title once, then remembers the output: no setup needed.
bool zoneForWiim(const String &wiimTitle, Zone &out);
// The WiiM's zone while Roon is feeding it, for progress and transport.
bool sourceZone(Zone &out);
String imageUrl(const String &imageKey, int size);

// The Settings row: "Automatic" learns the zone from the WiiM's title; a named zone pins it.
String zoneChoiceLabel();
void stepZoneChoice(int delta);

// Bumped on every track or cover change in any zone.
uint32_t version();

int copyList(List which, Row *out, int max);
uint32_t listsVersion();

// Non-blocking; the Roon task carries these out.
void requestPlay(List which, int index);
void requestControl(const char *control); // previous, next, playpause
void requestRefreshLists();

String statusJson();

} // namespace roon
