#pragma once

#include "NetConfig.h"

// Runs instead of the normal app, so the AP + captive page don't compete with WiiM TLS for RAM.
namespace net {
namespace portal {

constexpr const char *kApName = "WiimKnob-Setup";

// tick runs every loop pass so the screen and touch keep working. Never returns.
[[noreturn]] void run(const Config &current, void (*tick)());

} // namespace portal
} // namespace net
