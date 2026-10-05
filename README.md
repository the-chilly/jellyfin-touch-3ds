# Jellyfin Touch for New Nintendo 3DS / New 2DS

A personal homebrew adaptation of [bogocat/jellyfin-3ds](https://github.com/bogocat/jellyfin-3ds), based on commit `05bfa02f131d13f163bc7091d18af3b5b84f0820`. Licensed under GPL-3.0; upstream LICENSE and credits are preserved. This is an unofficial Jellyfin client.

**Status: the devkitARM build succeeded and host tests pass.** A compiled `.3dsx` is provided in the separate SD-card package. This adaptation has not been tested on a physical 3DS; playback, rendering and real SD-card behavior remain unverified. A `.cia` installer with an FBI download QR is also provided.

## Features

- Animated loading ring during playback startup, buffering, shelf and information loading. Foreground API requests poll in small slices to redraw while waiting; brief player/decoder setup can still pause drawing.

- Netflix 3DS-inspired charcoal-gray and blue browsing UI: selected poster and metadata on the top screen, three larger portrait posters below with a preview of the next shelf, and a blue selection outline, and resume progress bars.
- Persistent touchscreen tabs: Home, Library, Downloads, Settings. Downloads lists saved movies, episodes and songs with their SD file sizes. Touch **Play offline** or **Delete**. Download from Information (Y/touch) or Browse (X).
- Home shelves: Continue Watching, Movies, TV Shows, Music (albums), Recently Added. Shelves show up to 16 titles; X opens full libraries with normal pagination.
- Authenticated artwork and metadata load on separate artwork and metadata background threads. Encoded poster images are cached on SD, scoped by server and user, with a 128-file / 64 MiB maximum. Missing artwork uses a title placeholder. Failed requests retry automatically; L refreshes shelves and information; Settings can clear the shared cache.

- HTTPS with certificate and hostname verification across login, library API, artwork, audio, video and downloads.
- Movies, TV series/seasons/episodes, music artists/albums/tracks.
- Media information: title, year, runtime, episode season/number, series, artist, album, genres, rating and a scrollable description. Fields depend on your Jellyfin metadata.
- Touch timeline: tap a time, or drag to preview it and release to seek. Restarts once per gesture. Keeps a paused video paused after buffering. L/R still seek 30 seconds.
- Saved server URL (including reverse-proxy path), username, access token, user ID and device ID on the SD card. The password is never saved. Automatically restores the session on launch; revoked tokens require login again.
- Existing New 3DS H.264 hardware playback, audio streaming, pagination, search and offline cache.

## Build

With Docker installed and running:

```sh
./build.sh
```

The script uses `devkitpro/devkitarm:20260610`, installs 3DS dependencies, builds the pinned FFmpeg fork when needed, and produces `jellyfin-3ds.3dsx` plus an SD-ready folder under `dist/3ds/jellyfin-3ds/`. The first FFmpeg build can take approximately 15 minutes. Run `./build.sh clean` to rebuild the app from scratch.

For native devkitPro builds, install `3ds-dev`, `3ds-curl`, `3ds-mbedtls`, `3ds-zlib`, `3ds-mpg123`, `3ds-libopus`, `3ds-opusfile`, `3ds-libvorbisidec` and `3ds-libogg`. You also need the static FFmpeg libraries and headers in `lib/ffmpeg/`; the supplied FFmpeg build script targets the Linux Docker environment. Once those dependencies exist, run `make JFIN_VERSION=touch-0.5.0`.

Source and releases: [the-chilly/jellyfin-touch-3ds](https://github.com/the-chilly/jellyfin-touch-3ds). GitHub Actions can build the app; console behavior requires hardware testing.

## Install the compiled package

1. Use a New Nintendo 3DS or New Nintendo 2DS with Luma3DS and the Homebrew Launcher.
2. Extract `jellyfin-touch-3ds-sd.zip` and copy its `3ds/` folder to the root of your SD card. For a source build, use the generated `dist/3ds/` folder.
3. Ensure you have the console's DSP firmware at `sdmc:/3ds/dspfirm.cdc` (for example, dumped with DSP1).
4. Launch `jellyfin-3ds.3dsx` from the Homebrew Launcher.
5. Select each field with Up/Down and A. Enter your final server URL, username and password. Press R to log in. Settings save immediately after successful login.

Settings are stored in `sdmc:/3ds/jellyfin-3ds/config.ini`, with a `.bak` recovery copy. This adaptation shares that folder with upstream; back up existing settings before testing. The token stored there grants access to your account. Logout is available under Settings.

## HTTPS setup

CIA installations include this public CA bundle in the application. A custom SD bundle takes precedence. For Homebrew Launcher installations, copy the included `cacert.pem` to **`sdmc:/3ds/jellyfin-3ds/cacert.pem`** alongside the executable. This is the public CA bundle downloaded from [curl's Mozilla CA extract](https://curl.se/docs/caextract.html). Update it from that source when necessary. Certificate checking follows [curl's verification guidance](https://curl.se/docs/sslcerts.html).

For a private CA or self-signed server, append your trusted PEM CA certificate to that bundle. Use the DNS hostname covered by the certificate, and set the console's date/time correctly. The client never falls back to disabling certificate verification.

Use the final URL, e.g. `https://media.example.com/jellyfin`. Redirects are rejected to avoid sending credentials or stream tokens to another destination. Your reverse proxy must serve that address directly. HTTP is also supported when explicitly entered.

## Controls

| Screen | Controls |
| --- | --- |
| Login | Up/Down: field; A: keyboard; R: connect |
| Home | Touch: tap a poster to select; swipe to move; D-pad: titles/shelves; A: information; tap the bottom tabs to navigate |
| Home | X: full libraries; Y / Search touch button: search; L: refresh; SELECT: Settings; B/ZR: current playback |
| Browse | Touch: poster selection/swipes; D-pad: grid; A: information/open; B: back; Y: search; ZR: current playback |
| Browse | L/R: library pages; SELECT: Settings; X: download to SD |
| Information | Up/Down: description scroll; A: resume/play or open episodes/tracks; X: play from beginning; B: back |
| Playback | Touch timeline: seek; A: pause/resume; L/R: seek 30s; X: stop; B: browse/info |
| Playback | Down: hide controls; Up: show controls |
| Anywhere | START: exit |

Dragging previews a target while playback continues. Releasing reopens the stream at that position, so seeking requires buffering. B/X cancel an active drag before their usual action. Seeking is disabled when no duration is available.

## Exit behavior

Version touch-0.3.1 cancels idle audio/video, poster and metadata transfers before joining their workers. Stop flags use atomic access, and cleanup waits for existing GPU work instead of beginning a new frame after the HOME Menu closes the app. START and HOME Menu exit still require real-console verification. Modal loading screens also process HOME/sleep events before drawing, and the main loop skips rendering once close is requested. Shutdown stages are written to the debug log.

## Loading troubleshooting

Version touch-0.3.1 separates artwork downloads from shelf/description requests so a slow poster cannot hold the remaining shelves in the queue. Detail requests explicitly enable images/user data and request the overview. Temporary failures retry; the UI distinguishes failed information requests from a legitimately missing description.

If loading still stalls on the console, reproduce it and copy `3ds/jellyfin-3ds/debug.log` from the SD card before launching again (each launch resets it). Logs now record download/HTTP failures, decoded image dimensions and metadata results without logging request URLs or tokens.

## Verification and remaining limits

Run `python3 tests/run_host_tests.py` on a Mac/Linux host with Clang, curl development headers/library, Python 3 and OpenSSL. The test runner creates only temporary files and a loopback HTTPS server.

Passed here:

- No new drawing frame or playback start after a simulated HOME close during loading.
- Idle HTTPS transfer cancellation while headers or a response body are stalled; worker shutdown completes without waiting for the server timeout.

- Timeline limits, unknown duration, very short clips and overflow bounds.
- Actual UI input code with mock hardware: three-column poster taps, all four bottom tabs, Downloads placeholder/back navigation, gutter rejection, horizontal swipes, shelf/grid navigation, return from Settings, pending metadata playback protection, drag previews without reopening, one seek on release, cancel, failed seek, deferred pause and details navigation.
- Saved config, device ID stability, rejected newline injection, preservation of previous settings when writing fails, and backup recovery.
- Background artwork service: authenticated HTTPS, scoped SD cache round trips and capacity, malformed/oversized image rejection, stale metadata response rejection, all GPU operations on the frame thread, and metadata/shelves completing while a poster request is stalled, and worker cleanup while a metadata response is stalled.
- Real libcurl requests to a local HTTPS fixture: repeated frame callbacks throughout a stalled foreground request, no UI callbacks from background requests, home shelves, series-art fallback, login, metadata parsing, proxy base path, stream seek parameters, rejection of an untrusted CA, wrong hostname and redirects.

The application also compiled and linked successfully for ARM with the pinned devkitPro Docker environment. The build retains upstream FFmpeg enum-size linker notices; those are documented here rather than suppressed. The tests do **not** validate real SD-card behavior, Nintendo rendering, decoder performance, Jellyfin transcoding compatibility or actual A/V sync. Real hardware testing must cover a movie, several TV episodes, music, saved login after reboot, and at least ten consecutive seeks. Upstream has reported repeated-seek video issues; this adaptation does not claim to have resolved every decoder/transcode issue.

The server must permit transcoding and have a working FFmpeg installation. The existing video path requests small H.264/AAC MPEG-TS streams for the console. Old 3DS video and subtitle selection are outside this adaptation. CIA installation and launch still need testing on hardware.

## Credits

Original Jellyfin 3DS implementation by bogocat and contributors. Underlying projects include devkitPro/libctru, citro2d/citro3d, curl/mbedTLS, cJSON, FFmpeg/ThirdTube, mpg123, Opus, Vorbis and stb_image. Upstream also credits Switchfin and Video player for 3DS for implementation references.


## Experimental downloads — touch-0.4.0

Information has a **Y Download to SD card** button. Downloads shows up to 50 saved titles for the current server/account with file sizes, Play offline, and Delete. Progress shows received MB and a size estimate when the server omits a total; B cancels. Keep the console awake while downloading. Movies/episodes use the existing H.264/AAC MPEG-TS stream; songs use MP3. Completed media and title/description metadata persist on SD, scoped to server and account. Login tokens are not included in download metadata. Previously viewed posters load from the artwork cache; a missing cached image shows the title.

**Play offline** opens a local file and never falls back to streaming or reports playback to Jellyfin. The playback screen labels the source **SD card** or **Streaming**. Saved video supports the existing local-file seek path. Saved MP3 files now support local seeking and resume using the native MP3 decoder. Offline plays do not sync watched state. Downloads run in the foreground, with responsive cancel/HOME handling; there is no queue or interrupted-transfer resume yet. Each file is limited to 3.75 GiB. Transfers that fail, contain no media, have a short declared body, or fail to save are discarded. A nominally successful server response is not a guarantee that every frame of a long transcode is intact.

After a failed server connection at startup, saved titles for the remembered server/account open in Downloads. The initial connection attempt may take several seconds. Old unscoped cache files are not listed here; clear the cache in Settings and re-download them if needed. Deleting an active download stops playback first.

The host tests download a real 20-second generated H.264/AAC test movie and an MP3 over trusted HTTPS, compare their saved bytes, reopen metadata after cache initialization, and verify rejection/cancellation cases. The saved movie is decoded with audio and sought to 10 seconds using host FFmpeg. UI tests confirm touch actions, local video seeking, no streaming fallback on failure, and stop-before-delete. **These checks do not validate the New 3DS hardware decoder or a live Jellyfin transcode.** See the package's TEST-OFFLINE.txt for the console procedure.


## Updates — touch-0.5.1

In Settings, select or touch **Update**. The first press checks the latest public release from `the-chilly/jellyfin-touch-3ds`. If a newer version is available, press it again to install. B cancels a check or download. The app verifies the published SHA-256 digest, exact file size and 3DSX header before installing. Invalid or cancelled downloads leave the current executable intact. The previous executable is retained as `jellyfin-3ds.3dsx.bak`; exit and reopen through the Homebrew Launcher after installation. Keep the app in `/3ds/jellyfin-3ds/jellyfin-3ds.3dsx` for this updater. Login, cache, DSP firmware and settings are preserved. For a CIA installation, the button displays update instructions: install the latest CIA through FBI using the release QR. It does not overwrite an unrelated Homebrew Launcher executable.

Update requests use verified HTTPS without sending Jellyfin credentials. GitHub's latest-release API supplies release metadata and asset hashes ([API reference](https://docs.github.com/en/rest/releases/releases)). GitHub rate limits or missing network/certificates display an error. Update checks are manual. A release without the expected executable/hash is rejected. This is still an experimental build pending hardware testing.


## Offline resume and playback UI — touch-0.5.0

Saved movies, episodes and music store an offline bookmark on SD about every five seconds and on pause, back, stop, seek and application exit. Downloads offers **Resume offline** when a bookmark exists; Y starts from the beginning. Finishing near the end clears the bookmark. Checkpoints remain local and do not change Jellyfin watched state. A sudden power loss may lose the last few seconds. Existing downloads work without re-downloading; missing bookmark fields default to zero.

The interface uses charcoal-gray surfaces, light text and blue accents. Playback has a title/source header, scrubber with time labels, large touch controls for -30 seconds, Pause/Play and +30 seconds, and separate Back, Stop and Hide buttons. Up restores hidden controls. Download progress shows received/total MB, percent, smoothed speed and estimated time left. Unknown server sizes show an estimated total/percent explicitly; neither completion nor remaining time is guaranteed during transcoding. Partial downloads remain cancellable and are only saved after validation.

Host tests verify bookmark persistence/clearing, account isolation, touch controls and native MP3 decoding at the requested saved position. The UI preview is rendered from production draw calls with mock hardware; actual console fonts and rendering may differ. New 3DS hardware playback and SD behavior still need console validation.

![Playback controls preview](docs/playback-blue-preview.png)

Preview rendered from production UI draw calls on the host; console fonts may differ.

## HOME Menu installation (CIA)

On a New 3DS or New 2DS with custom firmware and FBI, open **FBI → Remote Install → Scan QR Code** and scan the QR in the [v0.5.1 release](https://github.com/the-chilly/jellyfin-touch-3ds/releases/tag/v0.5.1). It links directly to `jellyfin-3ds.cia`. Alternatively, copy the CIA to the SD card and install it from FBI. Launch **Jellyfin Touch** on the HOME Menu. Audio still requires your console's dumped `/3ds/dspfirm.cdc`; no copyrighted firmware is bundled.

The CIA shares the existing `/3ds/jellyfin-3ds/` settings and download folder, so URL, login token and offline progress are retained. Public HTTPS certificates are bundled; an existing `cacert.pem` on SD takes precedence for private CAs. Future CIA updates use FBI and the latest release QR; the automatic executable replacement remains available to Homebrew Launcher users.

Build the ARM executable with `./build.sh`, then run `./build-cia.sh`. The packaging script pins and verifies official makerom/bannertool downloads, includes an icon/banner, requests New 3DS memory and MVD access, and bundles the CA file. Packaging builds pinned makerom 0.19.0 source with the small `tools/makerom-version.patch` namespace fix (the published tool silently drops the minor title version). Bannertool 1.2.2 is pinned; Apple Silicon requires Rosetta for bannertool. Title ID: `000400000F4A3100`, version `0.5.1`.

CIA content, executable/icon/banner hashes and RomFS hashes were checked with ctrtool 1.3.0, and the extracted certificates match the source. Homebrew test signatures require custom firmware. Physical-console installation, HOME Menu launch, HTTPS and playback remain unverified.
