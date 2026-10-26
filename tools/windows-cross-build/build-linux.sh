#!/usr/bin/env bash
# Native Linux (x86_64) build of the Spiral303 VST3, Standalone and test binary.
# Runs inside the image from Dockerfile, which is the same Ubuntu release the
# artifacts are targeted at.
set -euo pipefail

SRC=${SRC:-/work/spiral303-linux}
BUILD=${BUILD:-/work/build-linux}
OUT=${OUT:-/work/out/linux}
JOBS=${JOBS:-$(nproc)}

if [ ! -f /src/CMakeLists.txt ] || [ ! -d /src/vendor/JUCE/modules ]; then
    echo "Mount the repo at /src with JUCE cloned into vendor/JUCE (see docs/BUILDING.md)." >&2
    exit 1
fi

# Untouched copy: the Windows script patches JUCEUtils.cmake in its own tree, and
# neither build should inherit the other's CMake cache.
mkdir -p "$(dirname "$SRC")"
rm -rf "$SRC"
mkdir -p "$SRC"
tar -C /src --exclude=./build -cf - . | tar -C "$SRC" -xf -

rm -rf "$BUILD"
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$JOBS" --target Spiral303_VST3 Spiral303_LV2 Spiral303_Standalone Spiral303Tests

rm -rf "$OUT"
mkdir -p "$OUT"
cp -a "$(find "$BUILD" -maxdepth 5 -name 'Spiral303.vst3' -type d)" "$OUT/"
cp -a "$(find "$BUILD" -maxdepth 5 -name 'Spiral303.lv2' -type d | head -1)" "$OUT/"
cp -f "$(find "$BUILD" -maxdepth 4 -path '*Standalone*' -name 'Spiral303' -type f | head -1)" "$OUT/Spiral303_Standalone"
cp -f "$(find "$BUILD" -maxdepth 4 -name 'Spiral303Tests' -type f | head -1)" "$OUT/"

# The suite builds an AlertWindow to check the Enter wiring, and a headless
# container has no X display to create its peer on: paint into a virtual one.
xvfb-run -a "$OUT/Spiral303Tests"

echo "Linux artifacts in $OUT"
