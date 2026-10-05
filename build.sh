#!/usr/bin/env bash
# Reproducible .3dsx build and SD-card folder packaging.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE="devkitpro/devkitarm:20260610"
if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required for this build script. Install/start Docker, or use the native build instructions in README.md." >&2
    exit 1
fi
docker info >/dev/null
if [[ "${1:-}" == "clean" ]]; then
    docker run --rm -v "$PROJECT_DIR:/src" -w /src "$IMAGE" make clean
fi
docker run --rm -v "$PROJECT_DIR:/src" -w /src "$IMAGE" bash -e -c '
    dkp-pacman -S --noconfirm --needed 3ds-curl 3ds-mbedtls 3ds-zlib \
        3ds-mpg123 3ds-libopus 3ds-opusfile 3ds-libvorbisidec 3ds-libogg
    if [ ! -f lib/ffmpeg/libavformat.a ]; then
        bash lib/ffmpeg/build-ffmpeg.sh
    fi
    make -j"$(nproc)" JFIN_VERSION=touch-0.5.1
'
SD_DIR="$PROJECT_DIR/dist/3ds/jellyfin-3ds"
mkdir -p "$SD_DIR"
cp "$PROJECT_DIR/jellyfin-3ds.3dsx" "$SD_DIR/"
cp "$PROJECT_DIR/jellyfin-3ds.smdh" "$SD_DIR/"
cp "$PROJECT_DIR/cacert.pem" "$SD_DIR/"
echo "Build complete. Copy the dist/3ds folder to the root of your SD card."
