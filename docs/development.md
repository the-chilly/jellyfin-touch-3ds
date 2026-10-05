# Build and verification

[Back to the README](../README.md)

This adaptation is based on upstream commit `05bfa02f131d13f163bc7091d18af3b5b84f0820` of [bogocat/jellyfin-3ds](https://github.com/bogocat/jellyfin-3ds). The current release is **touch-0.5.4**.

## Build the application

Install and start Docker, then run:

```sh
./build.sh
```

The script uses `devkitpro/devkitarm:20260610`, installs 3DS dependencies, builds the pinned FFmpeg fork when needed, and produces `jellyfin-3ds.3dsx` and the SD folder `dist/3ds/jellyfin-3ds/`. The first FFmpeg build can take approximately 15 minutes. Use `./build.sh clean` to rebuild the application from scratch.

For a native devkitPro build, install `3ds-dev`, `3ds-curl`, `3ds-mbedtls`, `3ds-zlib`, `3ds-mpg123`, `3ds-libopus`, `3ds-opusfile`, `3ds-libvorbisidec`, and `3ds-libogg`. Supply the FFmpeg libraries and headers in `lib/ffmpeg/`; its build script targets the Linux Docker environment. Then run:

```sh
make JFIN_VERSION=touch-0.5.4
```

## Build the CIA

After building the ARM executable:

```sh
./build-cia.sh
```

Packaging uses pinned makerom 0.19.0 source and bannertool 1.2.2, verified by SHA-256. The small `tools/makerom-version.patch` fixes a macro collision that causes the published makerom tool to drop the minor title version. Apple Silicon requires Rosetta for bannertool; other local platforms use the pinned Linux/amd64 Docker image.

The CIA includes the icon, banner, HOME Menu launch logo, public CA bundle, and New 3DS memory/MVD/SD permissions. Title ID: `000400000F4A3100`. Current title version: `0.5.4` (84). Custom firmware is required for homebrew signatures.

GitHub Actions builds the application and CIA. The manual release workflow also creates a QR pointing directly to the release's CIA asset. Pass `CIA_RELEASE_VERSION=vX.Y.Z` when packaging another release.

## Host tests

Run on Mac/Linux with Clang, curl development headers/library, Python 3, OpenSSL, FFmpeg, and mpg123 development headers/library:

```sh
python3 tests/run_host_tests.py
```

The runner uses temporary files and a loopback HTTPS server. Tests cover:

- Verified TLS, hostname/CA rejection, redirects, login, metadata, server base paths, and stream seek parameters.
- Saved settings, backup recovery, device IDs, and rejected input injection.
- Touch navigation, playback buttons, timeline bounds and drag behavior, pause preservation, and simulated HOME close.
- Independent artwork/metadata workers, scoped artwork caches, malformed images, and stalled-transfer cancellation.
- Download validation, cancellation, account isolation, saved metadata, local playback selection, and stop-before-delete.
- Offline bookmark persistence/clearing and native MP3 decoding at a saved position.
- A real generated 20-second H.264/AAC movie and MP3 downloaded over trusted HTTPS. Host FFmpeg decodes the saved movie with audio and seeks to 10 seconds.
- Release parsing, executable verification, and update installation safeguards.

The ARM build and existing host tests passed for v0.5.4. The build retains upstream FFmpeg enum-size linker warnings. CIA content/executable/icon/banner/RomFS hashes were checked with ctrtool 1.3.0; extracted certificates matched the source. The QR was decoded, and its public download matched the local CIA.

These checks do not verify physical-console installation, Nintendo rendering, decoder performance, real SD-card behavior, live Jellyfin transcoding, or audio/video synchronization.

## Console checks still needed

Install and launch the CIA, then test HTTPS, saved login after reboot, a movie, several TV episodes, music, and at least ten consecutive seeks. Check pause/resume, HOME Menu exit, and offline resume after reopening. Upstream has reported repeated-seek video issues; this adaptation does not claim to resolve every decoder/transcode issue.

Use the SD package's `TEST-OFFLINE.txt` for the offline procedure and the test movie included in earlier releases. A successful download does not guarantee that every frame of a long server transcode is intact.

## Storage and diagnostics

Settings live in `/3ds/jellyfin-3ds/config.ini`, with a `.bak` recovery copy. The saved token grants account access. Download metadata does not contain login tokens; media and bookmarks are scoped by server/account. Offline playback never falls back to streaming and does not report watched state to Jellyfin.

Artwork is cached with a 128-file / 64 MiB limit. Settings can clear the shared cache. Missing cached posters use a title placeholder. Old unscoped cache files are not listed as saved downloads.

Shutdown cancels transfers before joining workers, uses atomic stop flags, and avoids starting a drawing frame after HOME close. Loading screens process HOME/sleep events. Foreground network requests redraw while waiting, though decoder setup can briefly pause drawing.

For loading or shutdown failures, save `/3ds/jellyfin-3ds/debug.log` before restarting. Logs record request failures, image dimensions, metadata results, and shutdown stages without logging request URLs or tokens.

The v0.5.1 CIA omitted the HOME Menu launch logo and was reported to fail immediately with ErrDisp. Version 0.5.2 restores the standard homebrew logo. `tools/verify_cia.py` now rejects packages with missing launch assets or invalid hashes; it rejects the old CIA and accepts the corrected one. The corrected launch behavior still needs confirmation on hardware.

The v0.5.2 CIA launches successfully on the user's console, but starting an episode was reported to crash the MVD process while the same video works in 3DSX. Version 0.5.3 enables `CanShareDeviceMemory`, matching ThirdTube's video-client configuration. The package check verifies this permission in both capability blocks. This addresses a likely cause; CIA video playback still needs confirmation on hardware.

## Investigating download throughput

Version 0.5.4 configures a 64 KiB stdio buffer for media writes and requests a 64 KiB curl receive buffer, matching the video streaming path. The SD buffer remains allocated until the file closes. Failed/cancelled downloads still discard the partial file; metadata is saved only after the media is validated and renamed.

A completed or cancelled transfer logs `DL: perf` with total transfer time, time to the first HTTP response, time in file writes/final flush and UI callbacks, received chunk count, and buffer configuration. The following `DL: saved` or `DL: failed/cancelled` line includes the received byte count. It does not log URLs or tokens. Response timing includes connection/TLS startup; remaining transfer time can include networking, TLS processing and waiting for server-produced data. These measurements do not independently prove a server or Wi-Fi bottleneck.

To collect console measurements, start the same episode download, let it run for about 30 seconds, then press B to cancel or let it finish. Copy `/3ds/jellyfin-3ds/debug.log` before restarting the application. An offline download does not send a playback-start report, so absence from Jellyfin's active-playback dashboard does not prove that no request or transcode is running.

Run the production download benchmark with:

```sh
python3 tests/benchmark_download.py
```

It uses trusted loopback HTTPS and compares a ready-made 6.5 MiB fixture with a 256 KiB response deliberately paced at 50 KiB/s. It checks the saved bytes. Before/after runs both transfer the ready-made file in roughly 0.05–0.06 seconds and the paced file at 49.9 KiB/s. This demonstrates no fixed 50 KiB/s cap in the host code; it does not reproduce console TLS, SD hardware, GPU rendering or the user's Jellyfin server. The host measurements do not establish a console speed improvement.

The app downloads from the same progressive transcode endpoint used for playback. Jellyfin's [progressive stream implementation](https://github.com/jellyfin/jellyfin/blob/master/MediaBrowser.Controller/Streaming/ProgressiveFileStream.cs) waits when the encoder has not produced more data. Increasing client buffers cannot make such a source produce data faster. curl's buffer setting is a [best-effort request](https://curl.se/libcurl/c/CURLOPT_BUFFERSIZE.html).
