#pragma once

#include <WebServer.h>

#include "settings/Settings.h"

// Settings page and its JSON API; up only while the knob is awake, since deep sleep drops Wi-Fi.
namespace web {

WebServer &server(); // main.cpp hangs its debug routes on it

// wiimName labels this knob in other knobs' switchers; applySetting pushes a web change to the knob.
void begin(const String &wiimName, void (*applySetting)(settings::Item));
void service();

} // namespace web
