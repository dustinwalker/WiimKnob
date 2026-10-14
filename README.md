# WiimKnob

![WiimKnob banner](docs/banner.webp)

Firmware for the [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm)
that turns it into a small WiiM remote:

- **Album art is the home screen.** A tap, a knob turn or waking the screen brings the controls
  up over a darkened copy of the art, tinted with the cover's own color: volume in big digits
  with a red ring around the edge, title/artist/album, prev/play-pause/next, and a battery
  icon. After a few seconds without input the controls fade and the art is left with a
  scrolling "title • artist" line and a thin progress ring.
- With only the art showing, swipe left/right to skip tracks. A spinner shows until the WiiM
  reports the new track, and the old art dims until the new cover has downloaded.
- Turn the knob to change volume, with a light haptic buzz on taps via the board's DRV2605L.
- Five swipeable pages: **Now playing**, **Recently played** (hidden unless turned on in
  Settings), **Presets**, **Settings** and **Network**. Tap the art first to bring up the
  controls, then swipe to change pages.
- **Settings** (tap a row, turn the knob to adjust; saved in flash): brightness, how long the
  controls stay up, screen-off and sleep timeouts, and the Recently played page.
- **Network** shows the Wi-Fi name, IP, signal and WiiM, and holds the Wi-Fi setup button. Setup
  runs from your phone: the knob opens its own network with a QR code, and a page there picks
  the Wi-Fi network and the WiiM to control. It also opens by itself if the saved network
  can't be reached at boot.
- Power-aware: the screen dims 15 seconds before it turns off, polling slows and the CPU drops
  to 80MHz while it's dark, and the board deep-sleeps after a longer timeout. On USB power it
  stays awake and dims instead of turning off. Double-tapping the dark screen plays/pauses
  without waking it.
- Over-the-air updates, and a read-only status page at `http://wiimknob.local/status`.
- Handles accented/non-ASCII track and artist names correctly (see **Fonts** below) — the
  stock LVGL fonts only cover plain ASCII.

This is a first working version, not a finished product — see **Ideas for next steps** below.

## Screenshots

These show the original four-screen layout; the controls now sit over full-screen art.

<table>
<tr>
<td align="center"><img src="docs/screenshot-controls.jpg" width="260" alt="Controls screen: circular album art, volume badge, battery icon, title/artist, prev/play-pause/next pill, red volume ring"><br><sub>1. Controls</sub></td>
<td align="center"><img src="docs/screenshot-now-playing.jpg" width="260" alt="Now playing screen: full-screen album art"><br><sub>2. Now playing</sub></td>
<td align="center"><img src="docs/screenshot-volume.jpg" width="260" alt="Now playing screen while turning the knob: big volume number with a red ring over the album art"><br><sub>2. Turning the knob</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/screenshot-recent.jpg" width="260" alt="Recently played screen: album thumbnails in rows, tap to play"><br><sub>3. Recently played</sub></td>
<td align="center"><img src="docs/screenshot-presets.jpg" width="260" alt="Presets screen: Turntable, Headphone Mode, Jazz and Wiim Amp Ultra tiles"><br><sub>4. Presets</sub></td>
<td align="center"><img src="docs/screenshot-angle.jpg" width="260" alt="The knob viewed at an angle, showing the physical case"><br><sub>The hardware</sub></td>
</tr>
</table>

## Hardware background

Waveshare hasn't published a pinout or demo code for this board (its wiki page is a
placeholder as of writing). Everything in `include/Pins.h` and `src/display/PanelInit.h` comes
from a community reverse-engineering effort that measured the schematic and factory firmware
directly:
https://github.com/teetotum-rs/firmware/blob/main/docs/hardware/

Two things worth knowing going in:

1. **The board has two microcontrollers.** The ESP32-S3 (which this firmware runs on) owns the
   display, touch and one of the two physical encoders on the shaft. A second, classic ESP32
   owns the audio DAC, classic Bluetooth, and forwards knob turns to a *second* logical encoder.
   This project only uses the ESP32-S3 side — we don't need the board's own audio output since
   WiiM is doing the actual audio playback.
2. **The knob is not a quadrature encoder.** Each direction pulses its own GPIO low (one pulse
   per detent, 30 per revolution) rather than two lines in quadrature. `src/input/Knob.cpp`
   decodes it by sampling the pin levels, not by counting edges -- see **Knob and screen
   responsiveness** below for why.

## WiiM API

Uses WiiM's local HTTP API (`https://<device-ip>/httpapi.asp?command=...`), documented at
https://cvdlinden.github.io/wiim-httpapi/ and in WiiM's own PDF. It's HTTPS with a self-signed
certificate and only reachable on your LAN, so the firmware skips certificate validation
(`WiFiClientSecure::setInsecure()`) rather than trying to pin a cert that isn't meant to be
verified against a public CA.

Commands used: `getPlayerStatus` (play state, volume), `getMetaInfo` (title/artist/album/cover
URL), `setPlayerCmd:vol:<0-100>`, `setPlayerCmd:onepause`, `setPlayerCmd:next`,
`setPlayerCmd:prev`, `MCUKeyShortClick:<n>` (fire preset n), `getAllRoutines` and
`getStatusEx` (preset details, see **Presets**). Presets and recently played also use the
WiiM's UPnP `PlayQueue` service (plain HTTP on port 49152): `GetKeyMapping`, `BrowseQueue`,
`CreateQueue`, `PlayQueueWithIndex`.

All WiiM networking runs on two FreeRTOS tasks (`src/wiim/WiimTask.cpp`), pinned away from the
core that drives the display/touch/knob loop, so a slow or stalled network call can't freeze
the UI. One task only sends what you just did (buttons, presets, albums, volume) and polls
play state; everything slow (metadata, album art download, saving recently played albums,
refreshing presets) runs on the other. With a single task, a tap could sit queued behind a
multi-second album art download.

Two things about the connection itself:

- **Keep it open, and retry once.** A fresh TLS handshake costs 400-700ms on this CPU, so each
  task keeps a long-lived `WiFiClientSecure`/`HTTPClient` pair open across calls. The WiiM's
  web server drops idle keep-alive connections after ~5-10s, and a request that hits a
  just-closed one fails before the WiiM ever sees it, so those get retried once on a fresh
  connection. The command task's status poll (every 1.2s) is also what keeps its connection
  warm.
- **Only two TLS sessions at a time.** Each one pins ~40KB of internal RAM (this framework
  build can't put mbedTLS buffers in PSRAM). The background task closes its own connection
  before downloading album art, which needs a third.

On reuse (see the big comment in `WiimClient.h`): **both** objects have to be
persistent, not just the socket. `HTTPClient`'s destructor unconditionally closes its
connection, so a fresh `HTTPClient` per call — even one that reuses the same
`WiFiClientSecure` — silently undid the keep-alive every time; every request paid a full TLS
handshake (measured at 400-700ms on this CPU) instead of the ~15-45ms a genuinely reused
connection costs. That was the actual cause of the multi-second lag on volume/prev/next/pause
in early versions of this firmware, not the network or the WiiM itself.

## Presets

The WiiM's own `getPresetInfo` reports zero presets on this firmware generation: presets are
now "routines", lists of steps (switch input, switch output, load an EQ, set the subwoofer)
stored on the device. The presets screen reads which routine sits in each slot (UPnP
`GetKeyMapping`), joins that with the routine definitions (`getAllRoutines`), and labels each
tile by what it does: input first (`line-in`, or `ethernet`/`wi-fi` for network streaming,
depending on how the WiiM is connected), then a USB DAC output, otherwise just `EQ`. Tapping a
tile sends `MCUKeyShortClick:<slot>`, the same as pressing it in the WiiM Home app.

The list is read at boot and refreshed every 5 minutes, so edits in the app show up on their
own. The WiiM doesn't store preset artwork locally and doesn't report which preset is in
effect, so the red border marks the last preset fired from the knob, not the "active" one.

## Recently played

The WiiM Home app's recently played list lives in WiiM's cloud, not on the device, so the
knob keeps its own. Whenever a new album starts playing from a queue on the WiiM, the knob
saves a copy of that queue (`BrowseQueue`) plus a 68px thumbnail to flash (LittleFS, on the
`spiffs` partition), so the list survives deep sleep and reboots. Tapping a thumbnail hands the
saved queue back (`CreateQueue`) and starts it from the first track (`PlayQueueWithIndex`).
`ReplaceQueue` looks like the right call but only swaps a queue that's already loaded, so it
silently restarted the current album instead.

An album is identified by its title plus its main artist, worked out from the saved track list
(the most common artist across its tracks, ignoring "feat." guests). The WiiM only reports the
current *track's* artist, which can change within an album: on Daft Punk's *Random Access
Memories* most tracks are "Daft Punk feat. ..." and one is credited to "Thomas Bangalter"
alone, which used to save the same album several times.

The page is off by default (turn it on in Settings). While it's off nothing is recorded.

Limits worth knowing:

- **Only sources that play from a queue on the WiiM show up**: albums started from the WiiM
  Home app (Plex, Qobuz, etc.). Connect-style sources (Qobuz Connect, Spotify Connect, TIDAL
  Connect, AirPlay) stream from another app with no device-side queue, so there's nothing to
  save or replay.
- **Streaming links can expire.** Qobuz stream URLs in a saved queue carry an expiry time
  (hours out); Plex links point at your own server and don't. An old Qobuz entry may fail to
  replay.
- **Saved queues include whatever the WiiM had in them**, which for Plex includes the server's
  access token. They stay in the knob's flash and are never logged.

## Knob and screen responsiveness

Fast knob spins used to lag well behind the hand. Two separate causes, both measured on-device:

- **The knob was losing detents.** The first decoder counted falling edges in a GPIO interrupt
  with a 5ms per-line debounce. On a fast spin the edges ring, and both lines sometimes dip
  together for a moment: logging showed ~85% of edges being discarded, and the both-low dips
  counting as one clockwise *plus* one counter-clockwise detent, which cancel out. The decoder
  now samples both pins every 1ms on an `esp_timer`, only trusts a level that's held for 2ms,
  counts a detent when exactly one line is low after the knob was idle, ignores both-low, and
  needs a return to idle before the next detent. (The community teardown saw the same thing:
  edge counting gave 37-41 per revolution, polling the levels exactly 30.)
- **Redraws were slow.** Measured with a simulated fast spin (a detent every 15ms on the
  full-screen art page), the volume readout went from 21.5 fps with 86ms from detent to
  screen, to ~43 fps and ~19ms. What made the difference:
  - Restarting the readout's fade on every detent touched its opacity, which marks the whole
    210px circle dirty. Now only the digits and the changed slice of the ring redraw, and a
    timer starts the fade once the knob has been still for a second (this alone halved the
    per-frame cost).
  - A knob turn redraws immediately (`lv_refr_now`) instead of waiting for LVGL's next refresh
    tick, and that tick is 15ms instead of 30ms.
  - Album art is decoded at the screen's full 360px, so the full-screen page draws it 1:1
    instead of software-scaling it on every redraw.
  - The readout's circle is opaque: blending a translucent one over the art cost ~5ms of each
    ~20ms frame.
  - `loop()` re-applies the WiiM's play state, connection and volume every pass; the UI now
    skips values that haven't changed instead of re-laying out text and refreshing styles.

- **Full-screen swipes were transfer-bound.** Arduino_GFX sent 2KB at a time and waited on
  each, which cost 28ms of every full-screen frame (the ~4.5ms above was a small redraw). The
  panel is now fed from a task on core 0: each band is copied into two 8KB internal bounce
  buffers and sent by DMA while LVGL renders the next band (DMA can't read PSRAM at this
  clock). With `-O2`, full-screen frames went from ~49ms to ~28-40ms.
- **Album art is pre-scaled.** Covers are decoded, box-filtered (or upscaled, for small ones)
  to the screen's 360px once, and the tinted backdrop is built at the same time, so every art
  draw is 1:1. Art is double-buffered, so the screen never draws a buffer being rewritten.

Things that measured as *not* worth it: moving LVGL's draw buffers into internal RAM and
sending them by DMA directly (no measurable gain; rendering on the CPU is the bottleneck).

## Memory

Internal RAM (~320KB) is the scarce kind: each WiiM TLS connection needs a large contiguous
block of it, and album art downloads were failing for lack of it. LVGL's 48KB object pool and
the 18KB JPEG decoder live in the 8MB PSRAM instead, which left ~93KB internal free with no
measurable effect on frame times. `/status` reports the free internal heap and its low point.

## Power management

All of these reset on any touch or knob turn. The timeouts are adjustable on the Settings page;
the defaults come from `Config.h`.

- **Hide controls** (`CONTROLS_TIMEOUT_S`, default 8s): the controls fade, leaving the art.
- **Dim**, 15 seconds before the screen turns off: a quarter of the set brightness. A touch on
  a dimmed screen only brightens it; it doesn't reach the controls.
- **Screen off** (`SCREEN_TIMEOUT_S`, default 70s): the backlight turns off. Wi-Fi stays
  connected and turning the knob still changes volume. While it's dark the knob polls the WiiM
  every 3s (enough to keep the connection warm, so a command still goes out at once), reads
  track info every 10s, and runs the CPU at 80MHz. A double-tap toggles play/pause and leaves
  the screen off; a single tap wakes it to the art, a knob turn wakes it to the controls.
- **Sleep** (`DEEP_SLEEP_TIMEOUT_MIN`, default 10 min): the whole board deep-sleeps, cutting
  Wi-Fi and the CPU, which is the largest lever for battery life. A touch or knob turn wakes it
  by pulsing one of three GPIOs the chip watches for while asleep (see `src/power/Power.cpp`).
  Waking is a full reboot, so it takes a few seconds to reconnect.
- **On USB power** the knob stays awake: no screen-off or sleep (the screen dims instead), no
  modem sleep (which adds 100-400ms to every WiiM request), and status polls every 0.5s. That
  also keeps it reachable for over-the-air updates.

Deep sleep intentionally does not try to preserve any state (in-flight volume changes, current
track, etc.) — the display and WiiM state just repopulate over the first second or two after
wake, the same as a normal boot.

## Haptics

The board has a DRV2605L LRA haptic driver on the touch controller's I2C bus (address `0x5A`),
enabled via GPIO38. Per the community research this project relies on for its pinout, the chip
answers on I2C regardless of that pin's state but its output stage stays off — and everything
feels like no motor is attached — until GPIO38 is driven high. `src/haptics/Haptics.cpp` enables
it, configures the driver for LRA playback (not the ERM default), and fires a short "click"
effect on every prev/play-pause/next tap, preset or album tap, and double-tap play/pause. It deliberately skips the DRV2605L's calibration
registers (rated voltage, overdrive clamp, auto-cal) — the ROM-library default felt effect
works fine on this board without them, and getting those wrong is how you end up silently
over-driving the actuator.

## Fonts

The stock LVGL `lv_font_montserrat_16`/`_20` only include ASCII plus the icon symbols — verified
directly from their compiled-in glyph range tables, not assumed. Any accented character (é, ö,
ñ, etc., e.g. in a track or artist name) had no glyph to draw at all. `src/ui/fonts/` has two
regenerated replacements (`lv_font_montserrat_16_latin`/`_20_latin`, used for the title/artist
labels only) built from the same Montserrat-Medium.ttf + FontAwesome5 symbol set LVGL's own
build uses, with the main range extended to cover Latin-1 Supplement and Latin Extended-A
(`0x20-0x7F,0xA0-0x17F,0x2022`) — enough for French, German, Spanish, Nordic, Polish, Czech,
and most other Western/Central European text. The exact `lv_font_conv` command is in each
file's own header comment if you need to extend the range further (e.g. for Cyrillic or Greek).

Two more generated fonts live alongside them: `lv_font_montserrat_96_digits` (just 0-9, for the
big volume readout -- LVGL's built-in fonts stop at 48px) and `lv_font_preset_icons_26` (four
FontAwesome glyphs for the preset tiles).

## Battery gauge

The controls show a battery-shaped icon with a proportional, color-coded fill. This board has
no fuel-gauge chip and no charger-status pin, so the level is an estimate — see the comments in
`src/power/Battery.h`/`.cpp`:

- **Level**: read from `BATTERY_ADC_PIN` (GPIO1, a community-documented but Waveshare-unverified
  candidate for the battery sense pin) and converted through `BATTERY_DIVIDER_RATIO` and a
  generic LiPo voltage curve. Both the pin and the ratio need calibrating against a multimeter
  reading of your actual battery — the raw voltage is logged over serial every poll
  (`[battery] ...`) specifically so you can do that.
- **USB power**: GPIO1 measured 4.66-4.84V on USB and the cell's own 3.9-4.2V on battery, so the
  pin sits on the supply rail rather than the cell. Anything above a full cell (4.45V on, 4.35V
  off) means USB power, shown as a bolt on the icon. It can't tell charging from full, and the
  percentage holds at its last battery reading while plugged in.

## Setup

1. Copy `include/Config.h.example` to `include/Config.h` and fill in your Wi-Fi credentials and
   your WiiM's IP address (find it in the WiiM Home app under Device Info; a static DHCP
   reservation on your router means you won't need to update this later).
2. Install [PlatformIO](https://platformio.org/). **This needs Python 3.10 or newer** — the
   platform this project uses (see below) refuses to run under older Python. If your default
   `python3` is older (macOS ships 3.9 by default), install a newer one (e.g. `brew install
   python@3.11`) and create a venv for PlatformIO with it:

   ```bash
   python3.11 -m venv ~/.platformio-venv
   ~/.platformio-venv/bin/pip install platformio
   ```

   Use `~/.platformio-venv/bin/pio` in place of `pio` below (or add it to your `PATH`).
3. Build and upload:

   ```bash
   pio run -e esp32-s3-knob -t upload
   ```

   To flash: hold the **BOOT** button on the ESP32-S3R8 side, plug in via USB-C, then release.
   The Type-C port routes to either the ESP32-S3's native USB or the USB-UART bridge depending
   on cable orientation (per Waveshare's product page) — if upload doesn't find the port, try
   flipping the cable.

4. Open the serial monitor (`pio device monitor`) to watch it connect to Wi-Fi and start
   polling.
5. After that first USB flash, update over Wi-Fi while the knob is awake (or on USB power):

   ```bash
   pio run -e esp32-s3-knob-ota -t upload
   ```

   Set `OTA_PASSWORD` in `Config.h` to require a password (see `platformio.ini`). To change the
   Wi-Fi network or WiiM later, use **Network -> Hold to set up Wi-Fi** instead of reflashing;
   `Config.h`'s values are only the fallback until something is saved there.

   If upload fails with "This chip is ESP32, not ESP32-S3", the cable orientation reached the
   board's other microcontroller (nothing gets written): flip the USB-C plug at the board. If
   it fails with "No serial data received", the board is probably in deep sleep or stuck:
   unplug, hold **BOOT**, plug back in, release.

If the screen stays dark or PSRAM isn't detected at boot, check `platformio.ini`'s
`board_build.arduino.memory_type` / `board_build.psram_type` against what `esptool.py
flash_id` reports for your specific board revision — these straps are inferred from the
product page spec (16MB quad flash, 8MB octal PSRAM on the ESP32-S3R8 module) but Waveshare
could ship a revision with different wiring.

## Project layout

```
include/
  Pins.h            board pinout
  Config.h.example  Wi-Fi + WiiM settings template (copy to Config.h)
  lv_conf.h         LVGL configuration
src/
  main.cpp          setup/loop: Wi-Fi, polling, input wiring, OTA, the /status page
  display/          QSPI bus + ST77916 panel bring-up, DMA flush task, PWM backlight
  haptics/          DRV2605L LRA haptic feedback
  input/            knob (pulse decoding) and touch (CST816D) drivers
  net/              saved Wi-Fi/WiiM config and the phone setup portal
  power/            dim/screen-off/deep-sleep management, battery and USB-power detection
  settings/         the Settings page values, kept in NVS
  wiim/             WiiM HTTP/UPnP client, the two network tasks, album art JPEG
                    fetch/decode, and the recently played history
  ui/               the LVGL pages and gesture navigation (UiSettings.cpp: settings,
                    network and setup screens)
    fonts/            regenerated fonts with accented-character support (see Fonts above),
                      plus a small icon font for the preset tiles
```

## Ideas for next steps

- **Multiroom awareness**: the WiiM API exposes multiroom group info; the UI doesn't show or
  control it yet.
- **Charging vs full**: USB power is detected from the supply rail, but the board has no
  charger-status pin to tell charging from a full battery. Probing the charge IC for one would
  allow a real "charged" state.
- **Play/pause state for Connect sources**: Qobuz Connect reports its status as "none"
  whether playing or paused, so the knob infers it from whether the track position is moving
  (the icon catches up ~2s after pausing). Other Connect sources may behave the same way.
- **Wider Unicode coverage**: the regenerated fonts (see Fonts above) cover Western/Central
  European accents but not Cyrillic, Greek, or CJK — extend the `lv_font_conv` range if you
  need those.
