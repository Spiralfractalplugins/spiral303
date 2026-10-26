#!/usr/bin/env bash
# Cross-build the Spiral303 Windows VST3 + Standalone with MSVC under Wine.
# Runs inside the image from Dockerfile; the repo is mounted read-only at /src,
# this directory under /work/tc.
set -euo pipefail

SRC=${SRC:-/work/spiral303}
TC=${TC:-/tc}
BUILD=${BUILD:-/work/build-msvc}
OUT=${OUT:-/work/out/windows}
JOBS=${JOBS:-$(nproc)}

export PATH=/opt/msvc/bin/x64:$PATH
export WINEDEBUG=-all

# vendor/JUCE is gitignored, so the checkout has to exist in the mounted repo.
if [ ! -f /src/CMakeLists.txt ] || [ ! -d /src/vendor/JUCE/modules ]; then
    echo "Mount the repo at /src with JUCE cloned into vendor/JUCE (see docs/BUILDING.md)." >&2
    exit 1
fi

# /src is read-only and holds build artifacts; take a clean writable copy.
mkdir -p "$(dirname "$SRC")"
rm -rf "$SRC"
mkdir -p "$SRC"
tar -C /src --exclude=./build -cf - . | tar -C "$SRC" -xf -

# Keep a persistent wine server alive; starting one per compiler invocation is slow.
pgrep wineserver > /dev/null || wineserver -p
wine64 wineboot 2> /dev/null || true

# The wine prefix ships no usable fonts and JUCE's Windows font plug resolves
# its default sans through the **Verdana** family name - DejaVu-only prefixes
# leave every glyph blank and the GUI pixel tests fail. Two-step fix, idempotent:
# 1) DejaVu covers the scripts the UI uses;
# 2) genuine Microsoft corefonts (Verdana/Arial/Times/Courier) supply the family
#    names Windows defaults expect; downloaded from sourceforge, unpacked with
#    cabextract, copied into the prefix and registered per file. Wine builds its
#    font list from the registry, not from a directory scan.
FDIR=$HOME/.wine/drive_c/windows/Fonts
if [ ! -e "$FDIR/DejaVuSans.ttf" ]; then
    if [ ! -d /usr/share/fonts/truetype/dejavu ]; then
        apt-get update -qq && apt-get install -y -qq fonts-dejavu-core
    fi
    mkdir -p "$FDIR"
    cp /usr/share/fonts/truetype/dejavu/DejaVu*.ttf "$FDIR/"
fi
if [ ! -e "$FDIR/verdana.ttf" ] && [ ! -e "$FDIR/Verdana.ttf" ] && [ ! -e "/usr/share/fonts/truetype/msttcorefonts/Verdana.ttf" ]; then
    if ! command -v cabextract > /dev/null 2>&1; then
        apt-get update -qq && apt-get install -y -qq cabextract
    fi
    mkdir -p /work/corefonts && cd /work/corefonts
    for exe in verdan32 arial32 times32 courie32; do
        [ -e "$exe.exe" ] || curl -fsSL -o "$exe.exe" \
            "https://downloads.sourceforge.net/project/corefonts/the%20fonts/final/$exe.exe" \
            || { rm -f "$exe.exe"; continue; }
        cabextract -q -L -d "$exe-dir" "$exe.exe"
    done
    find /work/corefonts -iname "*.ttf" -exec cp {} "$FDIR/" \;
fi
# Registry list is mandatory; reg add overwrites, so repeat on every run.
KFONT="HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"
for f in "$FDIR"/*.ttf "$FDIR"/*.TTF; do
    [ -e "$f" ] || continue
    wine64 reg add "$KFONT" /v "$(basename "$f") (TrueType)" /t REG_SZ /d "$(basename "$f")" /f > /dev/null 2>&1
done

# cl with /Zi, and the PE binaries run through wine, need the DIA matcher plus the
# release and debug CRTs in the prefix - JUCE's VST3 helper builds in Debug config.
S32=$HOME/.wine/drive_c/windows/system32
if [ ! -e "$S32/msdia140.dll" ]; then
    cp "/opt/msvc/DIA SDK/bin/amd64/msdia140.dll" "$S32/"
    wine64 regsvr32 "C:\\windows\\system32\\msdia140.dll" || true
fi
for dir in \
    "$(find /opt/msvc/VC/Redist/MSVC -type d -path '*/x64/Microsoft.VC14*.CRT' | head -1)" \
    "$(find /opt/msvc/VC/Redist/MSVC -type d -path '*/debug_nonredist/x64/*DebugCRT' | head -1)" \
    "$(find /opt/msvc -type d -path '*/bin/*/x64/ucrt' | head -1)"
do
    [ -n "$dir" ] && cp "$dir"/*.dll "$S32/"
done

# Cross-building needs JUCE's nested VST3 helper configure to see the toolchain.
# Only this throwaway copy is touched; vendor/JUCE in the repo stays pristine.
python3 "$TC/patch_juce_vst3_helper.py" "$SRC/vendor/JUCE/extras/Build/CMake/JUCEUtils.cmake"

rm -rf "$BUILD"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TC/toolchain-msvc.cmake" \
    -DCMAKE_BUILD_TYPE=Release

# In JUCE 8 the "Spiral303" target is just the shared code; the formats are separate.
# The first pass can fail inside the nested helper build (its deep object dirs only
# get created partway through), so retry once before giving up.
build_target() {
    cmake --build "$BUILD" --target "$1" -j"$JOBS" || cmake --build "$BUILD" --target "$1" -j"$JOBS"
}

for t in Spiral303_VST3 Spiral303_LV2 Spiral303_Standalone Spiral303Tests; do
    build_target "$t"
done

rm -rf "$OUT"
mkdir -p "$OUT"
cp -a "$(find "$BUILD" -maxdepth 5 -name 'Spiral303.vst3' -type d)" "$OUT/"
cp -a "$(find "$BUILD" -maxdepth 5 -name 'Spiral303.lv2' -type d | head -1)" "$OUT/"
cp -f "$(find "$BUILD" -maxdepth 4 -name 'Spiral303.exe' -type f | head -1)" "$OUT/"
cp -f "$(find "$BUILD" -maxdepth 4 -name 'Spiral303Tests.exe' -type f | head -1)" "$OUT/"

# The offline render suite is also the smoke test that the PE binaries run at all.
wine64 "$OUT/Spiral303Tests.exe"

echo "Windows artifacts in $OUT"
