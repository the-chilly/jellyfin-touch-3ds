#!/usr/bin/env bash
# Package the existing ARM build into an installable HOME Menu title.
set -euo pipefail
cd "$(dirname "$0")"
if [[ "${1:-}" != "--container" && "$(uname -s)-$(uname -m)" != "Darwin-arm64" ]]; then
    command -v docker >/dev/null || { echo 'Docker is required. Run ./build.sh first.' >&2; exit 1; }
    docker run --rm --platform linux/amd64 -v "$PWD:/src" -w /src devkitpro/devkitarm:20260610 bash ./build-cia.sh --container
    exit
fi
[[ -f jellyfin-3ds.elf && -f jellyfin-3ds.smdh ]] || { echo 'Run ./build.sh first.' >&2; exit 1; }
cia_sha256() {
    if command -v sha256sum >/dev/null; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}
CIA_RELEASE_VERSION="${CIA_RELEASE_VERSION:-0.5.2}"
CIA_RELEASE_VERSION="${CIA_RELEASE_VERSION#v}"
[[ "$CIA_RELEASE_VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || { echo "Invalid CIA release version" >&2; exit 1; }
CIA_MAJOR="${BASH_REMATCH[1]}"; CIA_MINOR="${BASH_REMATCH[2]}"; CIA_MICRO="${BASH_REMATCH[3]}"
(( 10#$CIA_MAJOR <= 63 && 10#$CIA_MINOR <= 63 && 10#$CIA_MICRO <= 15 )) || { echo "CIA version out of range" >&2; exit 1; }
CIA_PACKED_VERSION=$(( (10#$CIA_MAJOR << 10) | (10#$CIA_MINOR << 4) | 10#$CIA_MICRO ))
TOOLS=dist/cia-tools
mkdir -p "$TOOLS" dist/romfs
# Build a pinned official makerom source with a small namespace fix. The
# published tool's VER_MINOR macro collides with the CIA version index.
curl -fLsS https://codeload.github.com/3DSGuy/Project_CTR/tar.gz/e8f5f529c54ff9b22a2491a480ffa69206bf7b19 -o "$TOOLS/makerom-source.tar.gz"
echo "6b757ab8b8e4715047db9ddeb91f89e10e7165c3e803315d9d131a9297ca4fe0  $TOOLS/makerom-source.tar.gz" | cia_sha256 -c -
mkdir -p "$TOOLS/source"
tar -xzf "$TOOLS/makerom-source.tar.gz" --strip-components=1 -C "$TOOLS/source"
patch -d "$TOOLS/source" -p1 < tools/makerom-version.patch
make -C "$TOOLS/source/makerom" -j4 deps
make -C "$TOOLS/source/makerom" -j4
cp "$TOOLS/source/makerom/bin/makerom" "$TOOLS/makerom"
if [[ "$(uname -s)-$(uname -m)" == "Darwin-arm64" ]]; then
    BANNER_MEMBER=mac-x86_64/bannertool # Requires Rosetta on Apple Silicon.
else
    BANNER_MEMBER=linux-x86_64/bannertool
fi
curl -fLsS https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip -o "$TOOLS/bannertool.zip"
echo "e4259c08fe8944ebadd5f4b96f9a8603e5427338074cfc46323fbbc3410d51ed  $TOOLS/bannertool.zip" | cia_sha256 -c -
unzip -ojq "$TOOLS/bannertool.zip" "$BANNER_MEMBER" -d "$TOOLS"
chmod +x "$TOOLS/makerom" "$TOOLS/bannertool"
cp cacert.pem dist/romfs/cacert.pem
"$TOOLS/bannertool" makebanner -i assets/icons/banner.png -a assets/audio_silent.wav -o banner.bnr
"$TOOLS/makerom" -f cia -o jellyfin-3ds.cia.part -elf jellyfin-3ds.elf \
    -rsf app.rsf -icon jellyfin-3ds.smdh -banner banner.bnr -target t \
    -ver "$CIA_PACKED_VERSION"
python3 tools/verify_cia.py jellyfin-3ds.cia.part
mv jellyfin-3ds.cia.part jellyfin-3ds.cia
cia_sha256 jellyfin-3ds.cia
echo 'CIA built. Install with FBI on a New 3DS/New 2DS with custom firmware.'
