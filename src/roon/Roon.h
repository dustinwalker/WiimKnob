#pragma once

#include <Arduino.h>
#include <vector>

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
  bool shuffle = false;
  String loop; // disabled, loop, loop_one
};

// One row of the Library page. Fixed-size so a hundred of them can live in PSRAM.
struct Row {
  char title[96];
  char subtitle[80];
  char imageKey[48];
};

enum class List { kHistory, kPlaylists, kRadio, kQueue, kFavorites };
constexpr int kListCount = 5;
constexpr int kMaxRows = 100;
constexpr int kMaxFavorites = 50;

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
// The same choices as a list, for the web page: "Automatic" first, then zones by name.
std::vector<String> zoneChoices(int &current);
void setZoneChoice(int choice);

// Bumped on every track or cover change in any zone.
uint32_t version();

int copyList(List which, Row *out, int max);
uint32_t listsVersion();

// Kept on the knob: Roon's add-on API can't set Roon's own favorites. These write flash, so
// call them from a task with an internal-RAM stack. toggle returns 1 added, 0 removed, -1 no track.
int toggleFavorite(const Zone &zone);
bool removeFavorite(int index);
bool isFavorite(const String &title, const String &artist);

// Non-blocking; the Roon task carries these out.
void requestPlay(List which, int index);
void requestControl(const char *control); // previous, next, playpause
void requestToggleShuffle();
void requestCycleRepeat(); // off, all, one
void requestRefreshLists();
// Starts Roon Radio from the WiiM zone's current track. takeRadioResult says how the last one
// went, once: -1 nothing new, 0 failed, 1 started.
void requestStartRadio();
int takeRadioResult();

String statusJson();

} // namespace roon
