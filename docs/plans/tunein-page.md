# TuneIn page

A page for picking TuneIn stations from the knob and playing them on the WiiM without Roon,
plus a Now playing layout that fits live radio.

## What already works

When a station is started from WiiM Home, the knob falls back to the WiiM's HTTP API. Volume,
play/pause, title and art work now. The progress ring hides because radio reports no duration.
The new pieces are browsing, favorites, and a Now playing layout for radio.

Roon's My Live Radio tab (Library -> Radio) can already play TuneIn stations through Roon. This
page is for when Roon isn't running or isn't the source.

## API findings

Checked by hand on 2026-10-23.

- `http://opml.radiotime.com/` answers over plain HTTP, and `render=json` returns JSON. The knob
  can fetch it without a TLS session, so it doesn't take one of the two internal-RAM TLS slots
  (see `WiimClient::closeConnection`).
- `Browse.ashx` with no arguments returns the top level: Local Radio, Music, Talk, Sports, By
  Location, By Language, Podcasts. Each entry is an `outline` with `type: "link"` (another
  `Browse.ashx` URL) or `type: "audio"` (a station).
- `Browse.ashx?c=local` geolocates by IP and returns stations grouped under `children` (FM, AM,
  internet). Stations carry `text`, `subtext`, `guide_id` (`s28585`), `image`, `bitrate`,
  `formats`, `reliability`, and sometimes `current_track`.
- `Tune.ashx?id=<guide_id>&render=json` returns direct stream URLs, one per format, e.g.
  `https://stream.wortfm.org/stream/high.mp3`. They're HTTPS, but the WiiM plays them, not the
  knob.
- `Describe.ashx?c=nowplaying&id=<guide_id>&render=json` returns the station name, the current
  show and a `ttl`. Good for a slow refresh of the subtitle.
- `Search.ashx?query=` works, but the knob has no keyboard, so search is out of scope.
- `Browse.ashx?c=presets` (TuneIn account favorites) returns an empty body without a signed-in
  session, with or without `username=`. TuneIn account favorites aren't reachable this way.
- Logos are on plain HTTP, but most of them are PNG: `cdn-radiotime-logos.tunein.com/s21793q.png`
  is 11 KB, the `g` size is 56 KB. The knob only decodes JPEG today (JPEGDEC).

## Spike results

Tested on 2026-10-26 against the WiiM Amp Ultra (firmware Linkplay.5.2.829663), first with
curl, then through the knob's `/tunein?id=` route.

- **`setPlayerCmd:play:<url>` plays the stream with no metadata.** `getPlayerStatus` reports
  mode 10, vendor `CustomPushUrl`, and the URL itself as the title. `getMetaInfo` has the URL as
  the title, `unknow` for artist and art, and stays that way. The WiiM ignores the stream's
  ICY titles, even though WORT sends `icy-metaint`. It plays `.pls` links fine.
- **UPnP `SetAVTransportURI` + `Play` with DIDL-Lite metadata works.** The WiiM reports mode 2
  and the title, artist and `albumArtURI` from the DIDL, in both `getPlayerStatus` and
  `getMetaInfo`. The knob's existing metadata poll picks them up with no changes, so title and
  artist already show on Now playing. Use this path.
- **UPnP rejects playlist links** (`.pls`) with error 714, "Invalid MIME-type or resource not
  find". The knob has to fetch the playlist and send the first `FileN=` or `http` line. That
  works: SomaFM Groove Salad played from its `.pls`'s first AAC stream.
- **The logo fails on the knob**, which counts it in `art_failures`, because it's a PNG. This
  confirms PNGdec is needed.
- **The metadata is fixed at play time.** The show name only changes when the station is
  started again, so the knob has to refresh the subtitle itself from `Describe.ashx`.
- **`Describe.ashx` sometimes returns a placeholder logo** (`_0q.png` for SomaFM). The
  `image` on the Browse row was the station's real logo. Store the Browse row's image with each
  favorite.
- A missing artist comes back as `unknow`. Treat it as empty.
- Internal heap stayed at about 61 KB minimum free through all of this. The UPnP and TuneIn
  requests are plain HTTP and use no TLS slot.

## Decisions

- **Its own page, not a Library tab.** Library stays hidden until Roon approves the knob, and it
  already has a Radio tab for Roon's stations. TuneIn goes in the swipe order after Library, with
  an on/off row in Settings like Recently played. Off by default.
- **Favorites live on the knob.** TuneIn account favorites need auth (see above). The knob keeps
  its own list in flash, the same way Library History does. Up to 30 stations, saved as
  `guide_id`, name and logo URL.
- **Tabs:** Favorites, Local, Browse. Browse drills down one level at a time, with a "‹ Music"
  back row at the top of each sublevel. Podcasts are hidden at the top level, since episodes
  need seek and a queue that this layout doesn't have.
- **Add or remove a favorite** by holding a station row. A short buzz and a star on the row
  confirm it.
- **Playing:** `Tune.ashx`, take the first mp3 or aac URL, unwrap it if it's a `.pls` or
  `.m3u`, then call `WiimClient::playStream`. That's UPnP `SetAVTransportURI` with the station
  name, show and logo as DIDL-Lite metadata. The knob remembers which `guide_id` it started, so
  Now playing can refresh the show.
- **Prev/next while a knob-started station plays** move to the previous or next favorite.
  Swipes do the same.

## Radio-style Now playing

Used when the knob started the station, or when the WiiM reports `durationMs == 0` on a
network source (not line-in or optical).

- Art: the station logo, scaled to fill. Falls back to the WiiM's art if the logo fails.
- Title line: the station name. Artist line: the WiiM's ICY title if it reports one, otherwise
  `current_track` or the show from `Describe.ashx`, refreshed at `ttl` but no faster than every
  60 s.
- No progress ring. A small "LIVE" label sits where the ring's start would be.
- Prev/next show only when there are favorites to step through.
- Knob volume and double-tap play/pause stay the same.

## Memory

- PSRAM (8 MB) holds the parsed rows, favorites, thumbnails and the decoded logo. 100 rows at
  about 250 bytes is 25 KB. 100 thumbnails at 48x48 RGB565 is 460 KB. That's the same budget
  `roon::thumbs` already uses.
- PNG decoding needs PNGdec (bitbank2, the same author as JPEGDEC). Its decoder state is about
  48 KB, and it has to go in PSRAM. Allocate it with `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
  and keep it out of `.bss`.
- Internal RAM: one plain `WiFiClient` socket for the TuneIn requests, a few KB. No new TLS
  session.

## Stream survey

On 2026-10-26, 80 stations from Local, Music and three genres went through `Tune.ashx`:

- 74 direct streams: 53 HTTPS, 21 HTTP. These play over UPnP as they are.
- 5 HTTP playlists (4 `.pls`, 1 `.m3u`). The knob unwraps these.
- 1 HTTPS `.m3u`. This is the only kind it can't unwrap. It falls back to
  `setPlayerCmd:play:` with no metadata.
- 0 HLS.
- Logos: 56 PNG, 24 JPEG.

## Versioning

The knob can't say which build it's running today. Before the first milestone:

- `scripts/version.py`, a PlatformIO pre-build script, bakes `git describe --tags --dirty
  --always` into `FW_VERSION`, e.g. `v1.1.0`, `v1.1.0-3-g4e8cbe1` or `v1.1.0-3-g4e8cbe1-dirty`.
  `-dirty` means the build had uncommitted changes.
- The Network page and `/status` (`"firmware"`) show it, so a glance or a curl says what's
  flashed.
- Each milestone ends with an annotated tag on `main` (`git tag -a v1.1.0 -m "Radio dial"`).
  Builds between tags show how far past the last milestone they are.
- Baseline: tag `7285aee`, the last commit before TuneIn, as `v1.0.0`.

## Milestones

Each step inside a milestone is a commit. A milestone is done when its checklist passes on the
knob, and then it gets its tag.

### v1.0.1 Version stamp (done, 2026-10-26)

`scripts/version.py`, `FW_VERSION` on the Network page and in `/status`, and the `v1.0.0`
baseline tag.

Check: `/status` shows `v1.0.0-N-g<sha>` after an OTA flash, and `-dirty` with an
uncommitted edit.

### v1.1.0 Radio dial (done, 2026-10-26)

Task stacks for TuneIn and thumbnails live in PSRAM. Keep large buffers out of `.bss`: a
43 KB static there starved the WiiM's TLS connection and broke metadata polling.

Browse and play from the knob.

1. The spike (done): `WiimClient::playStream`, `requestStream`, and the `/tunein?id=` route.
2. `src/tunein/TuneIn.{h,cpp}`: HTTP fetch plus an ArduinoJson filter for `Browse` and `Tune`,
   flattening `children` groups into rows, on its own small task like the Roon one. Rows use
   the same struct shape as `roon::Row`. Playlist unwrapping moves here from `main.cpp`.
3. PNG thumbnails: add PNGdec with its state in PSRAM, then generalize `roon::thumbs` to any
   URL and JPEG or PNG.
4. A shared tabbed-list UI pulled out of `UiLibrary.cpp`. The TuneIn page with Local and Browse
   tabs, drill-down and a back row. A Settings toggle and `pageEnabled` slot.

Check: turn the page on in Settings. Local lists stations near you with logos. Browse -> Music
-> a genre drills down and back. Tapping a station plays it, and Now playing shows its name. Do
that with WORT (HTTPS direct) and SomaFM Groove Salad (HTTP `.pls`). WiiM Home shows the
station sensibly. `heap_internal_min` in `/status` stays above 40 KB after scrolling 100 rows.

### v1.2.0 Favorites (done, 2026-10-26)

Favorites are a third TuneIn list, saved to `/tunein_favorites.json` as guide_id, name and
logo. They're written from the loop task, never from the TuneIn task. Starring drops the
subtitle, because Local's subtext is the show on air at that moment. The fonts have no star
glyph, so `lv_font_star_16` carries just U+F005. Rows on pages that support holding fire on
`SHORT_CLICKED`, so a hold doesn't also count as a tap. `/tunein?list=favorites` and `&star=N`
drive it over HTTP.

Checked over HTTP: starred WORT, Magic 98 and WVMO, which came back in that order after an OTA
reboot. Unstarring Magic 98 left the other two. Opening favorite 0 played WORT in UPnP mode
with the logo. `heap_internal_min` was 51.9 KB.

5. Favorites in flash, modeled on the Library History store, saving the Browse row's logo.
6. Hold a row to add or remove it, with a star and a buzz. A Favorites tab, first in the tab
   order.

Check: star three stations, reboot, and they're still there in order. Unstar one. Tapping a
favorite plays it.

### v1.3.0 Starred (done, 2026-10-28)

As built: Roon favorites are a fifth Roon list, `kFavorites`, in `/roon_favorites.json`.
History and Favorites share `playBySearch()`. The Library tab label is the star glyph, and the
tab row narrows its gaps to 10 px to fit five tabs. A hold on the art flashes a big star
(yellow when starred, gray when unstarred, from `lv_font_star_40`), and a small star sits
between the volume and the title while the controls show. `/star` drives it over HTTP:
`?toggle=1` acts like the hold, `&play=N` and `&remove=N` act on the starred tab.

Checked over HTTP on v1.2.0-6-gc447a2d:
- Starred three Jack White tracks while skipping through them. They came back in order after
  an OTA reboot.
- Toggling the playing track unstarred it, and toggling again starred it.
- With WPR News started from TuneIn Favorites, toggling unstarred and restarred the station.
- `play=0` played Love Interruption in Roon. `remove=1` dropped the middle track.
- `heap_internal_min` was 39.5 KB after switching between the station and Roon, just under
  the 40 KB target. Boot alone read 41 to 54 KB.
- Roon took 20 to 70 s to reconnect after each reboot. This happened before v1.3.0 too.

Hold the art on Now playing to star what's playing. Roon tracks go to a new Favorites tab on
the Library page, and knob-started TuneIn stations go to TuneIn's Favorites.

Roon's add-on API can't set Roon's own favorites. On 2026-10-26 a probe listed the action
menus for a playing track, and Roon offered only Play Now, Add Next, Queue and Start Radio.
These favorites live on the knob and don't show up in Roon's apps.

7. `roon::favorites`: up to 50 rows (title, artist, image key) in PSRAM, saved to
    `/roon_favorites.json`. The same `Row` and JSON shape as History. `toggleFavorite(zone)`
    and `isFavorite(title, artist)` match on the title plus the main artist, the way
    `recent::sameAlbum` does, so "Daft Punk, Pharrell Williams" counts as Daft Punk. Flash
    writes happen on the loop task, like TuneIn's.
8. Playing a favorite uses the History path: search Roon for title plus artist, take the
    matching track, then Play Now. Pull that out of `play()` into `playBySearch(row)` so
    History and Favorites share it.
9. A Favorites tab on Library, first in the tab order. Five text tabs don't fit the
    250 px tab row, so its label is the star glyph from `lv_font_star_16`. `kMaxTabs` goes
    to 5. Holding a row in it unstars, and tapping plays.
10. Hold on Now playing. A `LONG_PRESSED` handler on the art, which taps already leave free
    because they fire on `SHORT_CLICKED`.
    - Roon is the source: star or unstar the playing track.
    - A station the knob started: star or unstar it in TuneIn Favorites. This needs the
      knob to remember the guide_id it started, which v1.4.0 needed anyway, so that part
      moves here.
    - Anything else (WiiM queue albums, Spotify Connect, AirPlay, inputs): no buzz, and a
      short "Can't star this" toast.
    A buzz confirms the change. A small star sits beside the title while the playing item
    is starred, so you can tell before holding.

Check: hold the art on three Roon tracks. Each buzzes and shows the star. Reboot, and they're
in the Library Favorites tab in the order they were starred. Tap one and it plays in Roon.
Hold the art again on a starred track and it unstars. Start a station from TuneIn, hold the
art, and it appears in TuneIn Favorites. Hold the art on a WiiM Home album and you get the
toast with no buzz. A single tap still toggles the controls, and the screen-off double tap
still pauses.

Open questions:
- Search can pick the wrong version of a track (live, remaster) when a title repeats.
  History has the same weakness. Saving the album too and preferring a matching subtitle
  would narrow it.
- Prev/next stepping through favorites stays in v1.4.0, and only for stations.

### v1.4.0 On air

11. Radio Now playing: the station logo as art, a LIVE label instead of the ring, prev/next and
   swipes stepping through favorites, and the `Describe.ashx` show refresh. Treat `unknow` as
   empty.
12. Recently played skips knob-started stations.
13. README: a TuneIn section, and update the Pages list. Remove the `/tunein` debug route or
   keep it documented as a smoke test.

Check: the full-screen logo shows on a station started from the knob. Swipe left and right
steps through favorites. The show name updates when the hour turns. Roon playback still looks
the same as before.

## Testing

- Every flash goes over OTA. `curl wiimknob.local/status` confirms `firmware` before anything
  else.
- `/tunein?id=` stays as a scriptable smoke test until v1.3.0. It plays a station, then the
  WiiM's own `getMetaInfo` confirms what the WiiM sees, independent of the knob's UI.
- Watch `heap_internal_min` and `art_failures` in `/status` after each milestone's checklist.
- What only you can check: how the screen looks, gestures, and WiiM Home.

## Fallback play path

Not needed. The UPnP path gives full metadata. If a later firmware breaks it, the queue route
Recently played uses (`BrowseQueue`/`CreateQueue`, captured from a station started in WiiM
Home) is the next thing to try.

## Open questions

- Does `Browse.ashx?c=local` geolocate well enough from a home IP, or does it need a
  `latlon=` setting?
- Recently played may capture knob-started stations as one-track "albums". It probably should
  skip them.
- TuneIn's OPML API is unofficial and could change or start requiring a `partnerId`. The page
  should fail soft, with an empty state that says so.
