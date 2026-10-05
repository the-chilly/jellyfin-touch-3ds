# Build and verification

[Back to the README](../README.md)

This adaptation is based on upstream commit `05bfa02f131d13f163bc7091d18af3b5b84f0820` of [bogocat/jellyfin-3ds](https://github.com/bogocat/jellyfin-3ds). The current release is **touch-0.5.2**.

## Build the application

Install and start Docker, then run:

```sh
./build.sh
```

The script uses `devkitpro/devkitarm:20260610`, installs 3DS dependencies, builds the pinned FFmpeg fork when needed, and produces `jellyfin-3ds.3dsx` and the SD folder `dist/3ds/jellyfin-3ds/`. The first FFmpeg build can take approximately 15 minutes. Use `./build.sh clean` to rebuild the application from scratch.

For a native devkitPro build, install `3ds-dev`, `3ds-curl`, `3ds-mbedtls`, `3ds-zlib`, `3ds-mpg123`, `3ds-libopus`, `3ds-opusfile`, `3ds-libvorbisidec`, and `3ds-libogg`. Supply the FFmpeg libraries and headers in `lib/ffmpeg/`; its build script targets the Linux Docker environment. Then run:

```sh
make JFIN_VERSION=touch-0.5.2
```

## Build the CIA

After building the ARM executable:

```sh
./build-cia.sh
```

Packaging uses pinned makerom 0.19.0 source and bannertool 1.2.2, verified by SHA-256. The small `tools/makerom-version.patch` fixes a macro collision that causes the published makerom tool to drop the minor title version. Apple Silicon requires Rosetta for bannertool; other local platforms use the pinned Linux/amd64 Docker image.

The CIA includes the icon, banner, HOME Menu launch logo, public CA bundle, and New 3DS memory/MVD/SD permissions. Title ID: `000400000F4A3100`. Current title version: `0.5.2` (82). Custom firmware is required for homebrew signatures.

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

The ARM build passed for v0.5.2; the existing host tests passed for v0.5.1. The build retains upstream FFmpeg enum-size linker warnings. CIA content/executable/icon/banner/RomFS hashes were checked with ctrtool 1.3.0; extracted certificates matched the source. The QR was decoded, and its public download matched the local CIA.

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
