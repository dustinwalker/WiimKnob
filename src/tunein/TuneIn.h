#pragma once

#include <Arduino.h>

#include "roon/Thumbs.h"

// TuneIn's OPML directory: plain HTTP, no account. Stations play on the WiiM over UPnP.
namespace tunein {

struct Row {
  char title[96];
  char subtitle[80];
  char image[roon::thumbs::kKeyLen];
  char target[96]; // a Browse.ashx query for a link, a guide_id ("s21793") for a station
  bool station;
};

enum class List { kLocal, kBrowse };
constexpr int kListCount = 2;
constexpr int kMaxRows = 100;

enum class State { kIdle, kLoading, kReady, kFailed };

void begin();

// Non-blocking; open() drills into a link or plays a station.
void requestLoad(List which);
void requestOpen(List which, int index);
void requestBack(List which);

int copyList(List which, Row *out, int max);
State state(List which);
// Empty at the top level, else the title of the level above.
String parentTitle(List which);
// Bumped whenever a list's rows or state change.
uint32_t version();

// UPnP rejects playlist links (error 714); HTTPS ones are left alone to spare a TLS session.
String firstStreamIn(const String &url);

} // namespace tunein
