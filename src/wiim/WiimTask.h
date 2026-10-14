#pragma once

#include "RecentAlbums.h"
#include "WiimClient.h"

#include <lvgl.h>

// Runs all WiiM networking (status/metadata polling, album art fetch+decode, and
// prev/next/pause/volume commands) on its own FreeRTOS task, pinned away from the core that
// runs loop() (display, touch, knob). A cold HTTPS connection to the WiiM costs low seconds on
// this CPU -- doing that inline in loop() froze the whole UI for the duration of every single
// command. Everything here is non-blocking from the caller's side: requests just enqueue work,
// and reads just copy out the latest known state under a mutex.
namespace wiim {
namespace task {

// Starts the background task. Call once from setup().
void begin(const String &host);

// Non-blocking: enqueues the command for the background task to send as soon as it can.
void requestNext();
void requestPrevious();
void requestTogglePause();
void requestPreset(int key); // 1-based preset slot
void requestRecent(int slot); // recent::Entry::slot -- replays that saved album

// Non-blocking: only the latest value before each send matters, so rapid knob turns collapse
// into a handful of network calls instead of one per detent.
void requestVolume(int volume0to100);

// How often play state and volume are polled. Defaults to POLL_STATUS_MS from Config.h.
void setStatusPollMs(uint32_t ms);
// Defaults to POLL_METADATA_MS from Config.h.
void setMetadataPollMs(uint32_t ms);
void requestMetadataRefresh();

// Whether newly played albums are saved to Recently played (off while that page is hidden).
void setRecentCapture(bool on);

// Counters for the /status page.
struct Diagnostics {
  uint32_t metaOk = 0;
  uint32_t metaFailures = 0;
  uint32_t artFailures = 0;
  uint32_t lastMetaOkMs = 0;
};
Diagnostics diagnostics();

// Non-blocking snapshot reads, safe to call every loop() iteration.
bool isConnected();
PlayerState getPlayerState();
TrackMetadata getMetadata();
const lv_img_dsc_t *getAlbumArt();
const lv_img_dsc_t *getAlbumArtTinted();
// True from a track change until its album art has been downloaded (or a few seconds pass).
bool isArtPending();

// Bumped whenever title/artist/album art changes. Compare against a value you saved from a
// previous call to know whether it's worth re-reading getMetadata()/getAlbumArt().
uint32_t metadataVersion();

// Same pattern for the preset list: refreshed at startup and every few minutes after.
PresetList getPresets();
uint32_t presetsVersion();

// And for recently played albums (most recent first), saved whenever a new album starts.
recent::List getRecent();
const lv_img_dsc_t *getRecentThumbnail(int slot); // nullptr if that album had no art
uint32_t recentVersion();

} // namespace task
} // namespace wiim
