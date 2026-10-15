# WiimKnob

![WiimKnob banner](docs/banner.webp)

WiimKnob turns the [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm)
into a small remote for a WiiM streamer. The album art fills the round screen, the knob sets the
volume, and a swipe skips the track. It's a fork of
[xon94010/WiimKnob](https://github.com/xon94010/WiimKnob), and the credits are at the bottom.

## What it does

### The album art is the home screen

Most of the time you just see the cover. Tap the screen, turn the knob or wake it, and the
controls come up over a darkened copy of the art, tinted with the cover's own color. You get the
volume in big digits with a red ring, the title and artist, prev, play/pause and next, and
a battery icon.

Leave it alone for a few seconds and the controls fade. As they go, a band with the scrolling
title and artist fades in near the bottom. It runs edge to edge, so the round glass crops it and
it looks like part of the bezel.

A progress ring runs around the edge. It takes the strongest color in the cover and sits on a
dark band of the same hue, so it matches whatever's playing. Gray and black-and-white covers get
a white ring. It glides along as the song plays rather than ticking once a second.

### Swipe to skip

With only the art showing, swipe left or right to change tracks. A ⏭ or ⏮ flash with a spinner
stays up until the WiiM reports the new track. Then the old cover sits at 70% dim with three
bouncing dots until the new one has downloaded, and the new cover fades in.

### Five pages

Tap the art to bring up the controls, then swipe sideways between Now playing, Recently played,
Presets, Settings and Network. Recently played stays hidden until you turn it on in Settings.

On the Settings page you tap a row and turn the knob to change it. It has brightness, how long
the controls stay up, the screen-off and sleep timeouts, the Recently played page, and battery
calibration. Everything saves to flash.

The Network page shows the Wi-Fi name, IP address, signal and which WiiM it controls, and has
the button for setting up Wi-Fi again.

### Everything else

- The knob turns the volume, and taps get a light buzz from the board's DRV2605L haptic driver.
- It looks after the battery. The screen dims, then turns off, then the board deep-sleeps.
  Double-tapping the dark screen plays or pauses without waking it. On USB power it stays awake.
- It learns its own battery. One calibration run from full to empty replaces the generic LiPo
  curve and sets the low-battery cutoff.
- You can update it over Wi-Fi, and `http://wiimknob.local/status` shows Wi-Fi, memory,
  battery, the learned curve and a boot log.
- Track names draw in most of the world's scripts: accented Latin, Vietnamese, Cyrillic, Greek,
  Chinese, Japanese and Korean. The stock LVGL fonts only cover plain ASCII.

### A note on Roon

I mostly play to my WiiM Amp from Roon, so that's what this fork gets tested with. The knob only
talks to the WiiM, never to Roon, and a few things come out differently:

- Volume, play/pause, skipping and the progress ring work, since they all go through the WiiM.
- New covers lag. When Roon changes tracks, the WiiM reports the new title first and the new
  cover link a poll or two later. That gap is why the old art dims with bouncing dots while it
  waits.
- Recently played stays empty. Roon streams to the WiiM without a queue on the device, so there's
  nothing to save or replay.
- Firing a preset that switches the WiiM's input takes it away from Roon, the same as it would
  from the WiiM Home app.

If you use the WiiM Home app instead, everything above works as described.

## Screenshots

These show the original four-screen layout. The controls now sit over full-screen art.

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

## Building and flashing

1. Copy the config template:

   ```bash
   cp include/Config.h.example include/Config.h
   ```

   The build needs this file, but you don't have to fill in your Wi-Fi or WiiM. The knob asks for
   those on first boot. The rest of the file holds defaults for timeouts, polling and the battery,
   and they're fine as they are.

2. Install [PlatformIO](https://platformio.org/). It needs Python 3.10 or newer, and macOS ships
   3.9. If yours is older, install a newer Python and give PlatformIO its own venv:

   ```bash
   python3.11 -m venv ~/.platformio-venv
   ```

   ```bash
   ~/.platformio-venv/bin/pip install platformio
   ```

   Then use `~/.platformio-venv/bin/pio` wherever this README says `pio`.

3. Hold the BOOT button on the ESP32-S3R8 side, plug in USB-C, let go, and flash:

   ```bash
   pio run -e esp32-s3-knob -t upload
   ```

   The USB-C port reaches a different chip depending on which way the plug goes in. If the upload
   can't find the port, or says "This chip is ESP32, not ESP32-S3", flip the plug. Nothing gets
   written to the wrong chip. If it says "No serial data received", the board is probably asleep
   or stuck. Unplug it, hold BOOT, plug back in and let go.

4. If you want to watch it boot, `pio device monitor` shows the log.

If the screen stays dark or PSRAM isn't detected, compare `board_build.arduino.memory_type` and
`board_build.psram_type` in `platformio.ini` with what `esptool.py flash_id` reports. They're
based on the product page, which lists 16MB quad flash and 8MB octal PSRAM, and Waveshare could
ship a revision wired differently.

## Getting it on your Wi-Fi

You do this from your phone. There's no need to edit `Config.h`.

1. On first boot the knob opens its own network, `WiimKnob-Setup`, and shows a QR code.
2. Scan the code with your phone's camera to join. The password is in the code, and it's
   different every time, so someone has to be holding the knob to change its network.
3. A setup page opens on your phone. If it doesn't, go to `http://10.254.254.1`.
4. Pick your Wi-Fi network, type the password and tap Connect.
5. The knob joins and looks for WiiMs on your network. Pick yours and tap Save and restart.

The knob keeps all of this in its own flash, so it survives updates and deep sleep.

To change it later, go to the Network page and hold "Hold to set up Wi-Fi". The setup page has a
Keep current Wi-Fi button in case you change your mind. If nobody joins for 10 minutes, the knob
gives up and goes back to the saved network.

If the saved network is down when the knob boots, it shows "Can't reach Wi-Fi" for 30 seconds.
Tap to open setup. Otherwise it sleeps and tries again after 5, 10 and 20 minutes, then every 30.

You can still put `WIFI_SSID`, `WIFI_PASSWORD` and `WIIM_HOST` in `Config.h` if you'd rather not
use the phone. The knob only uses them until something gets saved through setup.

## Updating over Wi-Fi

After the first USB flash, update over the air while the knob is awake or plugged in:

```bash
pio run -e esp32-s3-knob-ota -t upload
```

Anything on your LAN can flash the knob while it's awake. To stop that, set `OTA_PASSWORD` in
`Config.h` and pass it at upload time. `platformio.ini` has the details.

## Hardware background

Waveshare hasn't published a pinout or demo code for this board. Its wiki page was still a
placeholder at the time of writing. Everything in `include/Pins.h` and
`src/display/PanelInit.h` comes from a community teardown that measured the schematic and the
factory firmware: https://github.com/teetotum-rs/firmware/blob/main/docs/hardware/

Two things to know going in.

The board has two microcontrollers. The ESP32-S3, which runs this firmware, owns the display,
the touch panel and one of the two encoders on the shaft. A second chip, a classic ESP32, owns
the audio DAC and classic Bluetooth, and forwards knob turns to a second logical encoder. This
project only uses the S3. The WiiM does the actual playback, so the board's own audio output
isn't needed.

The knob isn't a quadrature encoder. Each direction pulses its own GPIO low, one pulse per
detent and 30 per revolution. `src/input/Knob.cpp` decodes it by sampling pin levels rather than
counting edges. The responsiveness section below explains why.

## WiiM API

The knob talks to the WiiM's local HTTP API at `https://<device-ip>/httpapi.asp?command=...`,
documented at https://cvdlinden.github.io/wiim-httpapi/ and in WiiM's own PDF. It's HTTPS with a
self-signed certificate, reachable only on your LAN. There's no public CA to check it against, so
the firmware skips certificate validation with `WiFiClientSecure::setInsecure()`. Plain HTTP on
port 80 is closed.

Commands it uses:

- `getPlayerStatus` for play state and volume
- `getMetaInfo` for title, artist, album and cover URL
- `setPlayerCmd:vol:<0-100>`, `setPlayerCmd:onepause`, `setPlayerCmd:next`, `setPlayerCmd:prev`
- `MCUKeyShortClick:<n>` to fire preset n
- `getAllRoutines` and `getStatusEx` for preset details

Presets and recently played also use the WiiM's UPnP `PlayQueue` service, plain HTTP on port
49152, with `GetKeyMapping`, `BrowseQueue`, `CreateQueue` and `PlayQueueWithIndex`.

All the WiiM networking runs on two FreeRTOS tasks in `src/wiim/WiimTask.cpp`, on the other core
from the display, touch and knob. A slow network call can't freeze the screen. One task sends
what you just did, like a button, preset, album or volume change, and polls play state. The
other handles the slow stuff: metadata, album art downloads, saving recently played albums and
refreshing presets. With a single task, a tap could sit in line behind a multi-second art
download.

Two things about the connection itself.

Each task keeps its connection open and retries once. A fresh TLS handshake costs 400-700ms on
this CPU, so each task keeps a `WiFiClientSecure` and `HTTPClient` pair open between calls. The
WiiM drops idle connections after 5-10 seconds. A request that lands on one it just closed fails
before the WiiM ever sees it, so the knob retries those once on a new connection. The 1.2-second
status poll keeps the command task's connection warm.

Only two TLS sessions run at once. Each one ties up about 40KB of internal RAM, because this
framework build can't put mbedTLS buffers in PSRAM. The background task closes its own
connection before downloading album art, which needs a third.

Both objects have to stay alive for reuse to work, not just the socket. `HTTPClient` closes its
connection in its destructor no matter what. Early versions made a fresh `HTTPClient` per call
on top of a shared `WiFiClientSecure`, and every request quietly paid a full TLS handshake,
400-700ms instead of the 15-45ms a reused connection costs. That was the real cause of the
multi-second lag on volume and skip in those versions, not the network or the WiiM. The long
comment in `WiimClient.h` has the details.

## Presets

On this firmware generation the WiiM's own `getPresetInfo` reports zero presets. Presets are now
"routines", lists of steps like switching input or output, loading an EQ or setting the
subwoofer, stored on the device. The presets page reads which routine sits in each slot with
UPnP `GetKeyMapping`, matches it to the routine definitions from `getAllRoutines`, and labels
each tile by what it does. The input comes first, `line-in` or `ethernet`/`wi-fi` for streaming
depending on how the WiiM connects, then a USB DAC output, and otherwise just `EQ`. Tapping a
tile sends `MCUKeyShortClick:<slot>`, the same as pressing it in the WiiM Home app.

The knob reads the list at boot and every 5 minutes after, so edits in the app show up on their
own. The WiiM doesn't keep preset artwork on the device or say which preset is active, so the
red border marks the last preset fired from the knob.

## Recently played

The WiiM Home app's recently played list lives in WiiM's cloud, so the knob keeps its own.
Whenever a new album starts from a queue on the WiiM, the knob saves a copy of the queue with
`BrowseQueue`, plus a 68px thumbnail, to LittleFS on the `spiffs` partition. The list survives
deep sleep and reboots. Tapping a thumbnail hands the saved queue back with `CreateQueue` and
starts it from the first track with `PlayQueueWithIndex`. `ReplaceQueue` looks like the right
call, but it only swaps a queue that's already loaded, so it quietly restarted the current
album.

The knob identifies an album by its title plus its main artist, the most common artist across
the saved tracks with "feat." guests ignored. The WiiM only reports the current track's artist,
which can change within an album. On Daft Punk's *Random Access Memories* most tracks are "Daft
Punk feat. ..." and one is credited to Thomas Bangalter alone, which used to save the same album
several times.

The page is off by default, and nothing gets recorded while it's off. Some limits:

- Only sources that play from a queue on the WiiM show up. That means albums started from the
  WiiM Home app, like Plex or Qobuz. Connect sources such as Qobuz Connect, Spotify Connect,
  TIDAL Connect and AirPlay stream from another app with no queue on the device, so there's
  nothing to save.
- Qobuz stream links in a saved queue expire after some hours, so an old Qobuz entry may not
  replay. Plex links point at your own server and keep working.
- A saved queue holds whatever the WiiM had in it, which for Plex includes the server's access
  token. It stays in the knob's flash and never goes to the log.

## Knob and screen responsiveness

Fast knob spins used to lag well behind your hand. There were two causes, both measured on the
device.

The knob was losing detents. The first decoder counted falling edges in an interrupt with a 5ms
debounce per line. On a fast spin the edges ring, and both lines sometimes dip together for a
moment. Logging showed about 85% of edges thrown away, and the both-low dips counted as one
clockwise and one counter-clockwise detent, which cancel out. The decoder now samples both pins
every 1ms on an `esp_timer` and only trusts a level held for 2ms. It counts a detent when exactly
one line goes low from idle, ignores both-low, and waits for idle before the next one. The
community teardown found the same thing: edge counting gave 37-41 per revolution, polling the
levels gave exactly 30.

Redraws were slow. With a simulated fast spin, a detent every 15ms on the full-screen art page,
the volume readout went from 21.5 fps and 86ms from detent to screen to about 43 fps and 19ms.
Here's what made the difference:

- Restarting the readout's fade on every detent touched its opacity, which marked the whole
  210px circle for redraw. Now only the digits and the changed slice of the ring redraw, and a
  timer starts the fade once the knob has been still for a second. That alone halved the cost
  of each frame.
- A knob turn redraws right away with `lv_refr_now` instead of waiting for LVGL's next tick,
  and the tick is 15ms instead of 30ms.
- Album art decodes at the screen's full 360px, so the art page draws it 1:1 instead of scaling
  it in software on every redraw.
- The readout's circle is opaque. Blending a translucent one over the art cost about 5ms of each
  20ms frame.
- `loop()` pushes the WiiM's play state, connection and volume to the UI on every pass. The UI
  now skips values that haven't changed instead of re-laying out text and refreshing styles.

Full-screen swipes were limited by the transfer to the panel. Arduino_GFX sent 2KB at a time and
waited on each chunk, which cost 28ms of every full-screen frame. Now a task on core 0 feeds the
panel. It copies each band into two 8KB internal bounce buffers and sends them by DMA while LVGL
renders the next band. DMA can't read PSRAM at this clock, hence the bounce buffers. With `-O2`
as well, full-screen frames went from about 49ms to 28-40ms.

Album art gets scaled once. The knob decodes each cover, box-filters it down to 360px or
upscales small ones, and builds the tinted backdrop at the same time, so every art draw is 1:1.
The art is double-buffered, so the screen never draws from a buffer that's being rewritten.

One thing that didn't help: moving LVGL's draw buffers into internal RAM and sending them by DMA
directly. Rendering on the CPU is the bottleneck, and it made no measurable difference.

## Memory

Internal RAM, about 320KB, is the scarce kind. Each WiiM TLS connection needs a big contiguous
block of it, and album art downloads used to fail for lack of it. LVGL's 48KB object pool and the
18KB JPEG decoder now live in the 8MB PSRAM. That left about 93KB of internal RAM free, with no
measurable effect on frame times. `/status` reports the free internal heap and its low point.

## Power management

Any touch or knob turn resets all of these. You can change the timeouts on the Settings page,
and the defaults come from `Config.h`.

- **Hide controls**, after 8 seconds by default. The controls fade and leave the art.
- **Dim**, 15 seconds before the screen turns off, to a quarter of the set brightness. Touching a
  dimmed screen only brightens it. The touch doesn't reach the controls.
- **Screen off**, after 70 seconds by default. The backlight goes off, Wi-Fi stays connected,
  and the knob still changes volume. While the screen is dark the knob polls play state every 3
  seconds, enough to keep the connection warm so a command still goes out at once. It stops
  reading track info, since each read was a fresh TLS handshake, and catches up on wake. It also
  checks touch every 20ms instead of 5ms and drops the CPU to 80MHz. A double-tap toggles
  play/pause and leaves the screen off. A single tap wakes it to the art, and a knob turn wakes
  it to the controls.
- **Sleep**, after 10 minutes by default. The board deep-sleeps, which shuts down Wi-Fi and the
  CPU. A touch or knob turn wakes it by pulsing one of three GPIOs the chip watches while asleep;
  `src/power/Power.cpp` has the details. Before it sleeps, the panel controller goes into its own
  sleep mode, the haptic driver goes into standby with its enable pin held low, and the current
  cover saves to flash if it changed. Waking is a full reboot. The saved cover shows dimmed
  while the knob reconnects to the access point it used last, skipping the scan, and the detent
  that woke it counts as a volume step once the volume is known.
- **On USB power** the knob stays awake. There's no screen-off or sleep, just the dim. Modem
  sleep is off too, since it adds 100-400ms to every WiiM request, and status polls every half
  second. It also stays reachable for over-the-air updates.
- **Low battery.** If the voltage stays under the cutoff for 15 seconds, the knob shows "Battery
  low" and sleeps. A wake within 0.05V of the cutoff goes straight back to sleep without starting
  Wi-Fi. The cutoff is 3.45V until a calibration run learns one for your battery.
- **Wi-Fi down at boot.** See [Getting it on your Wi-Fi](#getting-it-on-your-wi-fi).

The board's other chip, the classic ESP32 running Waveshare's audio and Bluetooth firmware,
keeps running through the S3's deep sleep. Nothing documented on the link between them can put
it to sleep, so it's the most likely drain on a battery left asleep overnight.

`/status` shows why the knob last booted and keeps a 16-entry boot log in NVS with the start
reason, wake source, time asleep, and voltage at sleep and at wake. The log survives a flat
battery. A 15-second loop watchdog reboots a hung firmware rather than leaving the screen lit.

## Battery gauge

The controls show a battery icon with a color-coded fill. The board has no fuel-gauge chip and
no charger-status pin, so the level is an estimate. The comments in `src/power/Battery.h` and
`Battery.cpp` go into more detail.

The knob reads the voltage on `BATTERY_ADC_PIN`, GPIO1, which the community documented but
Waveshare hasn't confirmed. It scales the reading by `BATTERY_DIVIDER_RATIO` and turns it into a
percentage with a voltage curve. That's a generic LiPo curve until you calibrate. The raw
voltage goes to the serial log on every poll as `[battery] ...`, so you can check the divider
ratio against a multimeter.

GPIO1 reads 4.66-4.84V on USB and the cell's own 3.9-4.2V on battery, so the pin sits on the
supply rail rather than the cell. Anything above a full cell means USB power, and the icon shows
a bolt. The threshold is 4.45V going on and 4.35V going off. The knob can't tell charging from
full, and the percentage holds at its last battery reading while it's plugged in.

### Calibration

A battery's voltage curve depends on the cell and the load. The generic curve can read 90% for
hours and then fall off a cliff. Settings -> Calibrate battery learns the real one:

1. Charge fully, unplug, and turn the setting on. The knob stays awake with the screen on so the
   load stays steady, and the battery icon's outline turns orange.
2. It logs the voltage to flash once a minute, so a reboot or an update doesn't lose the run.
3. The low-battery shutdown ends the run, and that point counts as 0%. The knob turns the log
   into an 11-point curve, 100% down to 0% in steps of 10, and sets its low-battery cutoff a
   little above where the battery gave out. Both are saved to NVS.
4. Plugging in early cancels a run that hadn't got near empty and finishes one that had. If the
   run ended in a reset instead of a clean shutdown, the next boot recovers the log.

My battery's run took 222 minutes and settled on a 3.45V cutoff. `/status` shows the learned
curve, the run length and the cutoff in use.

## Haptics

The board has a DRV2605L haptic driver on the touch controller's I2C bus at `0x5A`, enabled by
GPIO38. According to the community teardown, the chip answers on I2C whatever that pin is doing,
but its output stage stays off until GPIO38 goes high. Until then it feels like there's no motor
at all. `src/haptics/Haptics.cpp` enables it, sets it up for LRA playback instead of the ERM
default, and fires a short click on prev, play/pause and next, preset and album taps, and the
double-tap play/pause.

It skips the DRV2605L's calibration registers for rated voltage, overdrive clamp and auto-cal on
purpose. The default click from the ROM library feels right on this board without them, and
getting them wrong is how you end up quietly overdriving the motor.

## Fonts

The stock LVGL `lv_font_montserrat_16` and `_20` only include ASCII and the icon symbols. I
checked their compiled-in glyph tables. Any accented character, the é or ö or ñ in a track or
artist name, had no glyph to draw.

`scripts/make_fonts.py` builds the replacements in `src/ui/fonts/` as a chain. LVGL looks for
each character in the first font and falls back to the next one when it's missing:

1. `lv_font_montserrat_16_latin`, `_18_latin` and `_20_latin`, for the title, artist and the
   scrolling band. They're the same Montserrat Medium LVGL ships, extended to Latin-1, Latin
   Extended-A, Cyrillic and Vietnamese. The 16 and 20 also keep LVGL's FontAwesome icons.
2. `lv_font_noto_greek_16`, `_18` and `_20`, the Greek letters from Noto Sans Medium.
3. `lv_font_noto_cjk_18_1`, `_2` and `_3`, from Noto Sans CJK JP Medium. The first two split
   about 7,200 Chinese characters between them,
   the level-1 sets of GB2312, Big5 and JIS X 0208, so it covers common simplified, traditional
   and Japanese text. The third has the 2,350 everyday Korean syllables, kana and full-width
   punctuation. It's one font split three ways so each generated file stays under 5MB.

English titles never touch the Noto fonts. There's only one CJK size, shared by all three
labels, because each size costs about 1.2MB of flash. For the same reason it's the only
compressed font, which is why `lv_conf.h` turns on `LV_USE_FONT_COMPRESSED`. With it, the
firmware takes about 3.2MB of its 6.4MB slot.

Chinese and Japanese share character codes, and some characters are drawn a little differently
in each. The JP version of Noto draws them the Japanese way, which still reads fine as Chinese.

The script downloads the Noto fonts into `.pio/fonts` on first run. It needs `npx` and a prior
`pio run`, so the LVGL library with Montserrat is already in place.

Two more generated fonts sit alongside them. `lv_font_montserrat_96_digits` has just 0-9 for the
big volume readout, since LVGL's built-in fonts stop at 48px. `lv_font_preset_icons_26` has four
FontAwesome glyphs for the preset tiles.

## Project layout

```
include/
  Pins.h            board pinout
  Config.h.example  defaults and optional Wi-Fi/WiiM fallback (copy to Config.h)
  lv_conf.h         LVGL configuration
src/
  main.cpp          setup/loop: Wi-Fi, polling, input wiring, OTA, the /status page
  display/          QSPI bus + ST77916 panel bring-up, DMA flush task, PWM backlight
  haptics/          DRV2605L haptic feedback
  input/            knob (pulse decoding) and touch (CST816D) drivers
  net/              saved Wi-Fi/WiiM config and the phone setup portal
  power/            dim/screen-off/deep-sleep, battery, USB-power detection, calibration
  settings/         the Settings page values, kept in NVS
  wiim/             WiiM HTTP/UPnP client, the two network tasks, album art fetch/decode,
                    and the recently played history
  ui/               the LVGL pages and gesture navigation (UiSettings.cpp: settings,
                    network and setup screens)
    fonts/            generated fonts for most scripts, plus icon fonts
scripts/
  make_fonts.py     regenerates the text fonts
```

## Ideas for next steps

- **Multiroom.** The WiiM API exposes multiroom groups, and the knob doesn't show or control
  them yet.
- **Charging vs full.** The knob spots USB power from the supply rail, but with no
  charger-status pin it can't tell charging from a full battery. Probing the charge IC for one
  would give a real "charged" state.
- **Play/pause for Connect sources.** Qobuz Connect reports "none" whether it's playing or
  paused, so the knob guesses from whether the track position is moving. The icon catches up
  about 2 seconds after a pause. Other Connect sources may do the same.
- **Roon as a source.** Roon's extension API pushes track changes and serves covers at any size,
  which would fix the art lag above. It needs a C++ client for Roon's discovery and messaging
  protocols, since Roon only publishes a Node.js one.
- **Rare characters.** The CJK font only holds the common sets, so an uncommon character in a
  name shows as a blank. Arabic, Hebrew, Thai and the Indic scripts aren't covered at all, and
  Arabic and Hebrew would also need right-to-left layout.

## Credits

The original WiimKnob firmware, the banner and the screenshots are by
[xon94010](https://github.com/xon94010/WiimKnob). The board pinout and panel bring-up come from
the [teetotum-rs community teardown](https://github.com/teetotum-rs/firmware/blob/main/docs/hardware/).
