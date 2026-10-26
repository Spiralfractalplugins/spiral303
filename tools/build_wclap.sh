#!/usr/bin/env bash
# Builds the WebAssembly version of Spiral303: a CLAP plugin compiled to
# wasm32 and packed into a .wclap bundle.
#
#   tools/build_wclap.sh [--debug] [--skip-tests]
#
# The wasi-sdk is downloaded into vendor/ if it is not there yet, pinned by
# version and checksum. The result lands in dist/:
#
#   dist/Spiral303-<version>-wclap.tar.gz   the bundle, ready to unpack
#   dist/Spiral303-<version>-wclap/          the same, unpacked
#   dist/Spiral303-<version>-wclap-registry.tar.gz
#           the same files with no top directory, for the plinken.org catalog
#           (see docs/PUBLISHING.md)
#
# The script also runs the smoke test: the module is loaded by a small host
# (wclap/tools/wclap-host.mjs) and has to make sound.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

WASI_SDK_VERSION=34
WASI_SDK_TAG="wasi-sdk-${WASI_SDK_VERSION}"
BUILD_TYPE=Release
RUN_TESTS=1

for arg in "$@"; do
    case "$arg" in
        --debug) BUILD_TYPE=Debug ;;
        --skip-tests) RUN_TESTS=0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# --- which sdk do we need? ---------------------------------------------------

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)  SDK_PLATFORM=arm64-macos;   SDK_SHA=9c59398106b417f8f14913380fdf0097a8cc0ff4af9eb3ce0065a859e88d49e9 ;;
    Darwin-x86_64) SDK_PLATFORM=x86_64-macos;  SDK_SHA=87d27fa8adc68dee59bfbf2e22a6d34ef717c34d6bf1d8af2a56fc929d9ce0eb ;;
    Linux-aarch64) SDK_PLATFORM=arm64-linux;   SDK_SHA=f7e243dff54d60bcc576e94d6166b69f410f2500ae4a9ceef34315be10e77971 ;;
    Linux-x86_64)  SDK_PLATFORM=x86_64-linux;  SDK_SHA=b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4 ;;
    *)
        echo "no wasi-sdk build for $(uname -s)-$(uname -m)" >&2
        echo "the archives are at https://github.com/WebAssembly/wasi-sdk/releases" >&2
        exit 1
        ;;
esac

# the sdk unpacks as wasi-sdk-<version>.0-<platform>
WASI_SDK_DIR="vendor/wasi-sdk-${WASI_SDK_VERSION}.0-${SDK_PLATFORM}"

SDK_ARCHIVE="wasi-sdk-${WASI_SDK_VERSION}.0-${SDK_PLATFORM}.tar.gz"
SDK_URL="https://github.com/WebAssembly/wasi-sdk/releases/download/${WASI_SDK_TAG}/${SDK_ARCHIVE}"

# --- the sdk ----------------------------------------------------------------

if [ ! -d "$WASI_SDK_DIR" ]; then
    echo "fetching the wasi-sdk (${SDK_PLATFORM})"
    mkdir -p vendor
    curl -L --fail --retry 3 -o "vendor/${SDK_ARCHIVE}" "$SDK_URL"

    echo "checking the checksum"
    if command -v sha256sum >/dev/null 2>&1; then
        actual="$(sha256sum "vendor/${SDK_ARCHIVE}" | cut -d' ' -f1)"
    else
        actual="$(shasum -a 256 "vendor/${SDK_ARCHIVE}" | cut -d' ' -f1)"
    fi
    if [ "$actual" != "$SDK_SHA" ]; then
        echo "the checksum does not match:" >&2
        echo "  expected $SDK_SHA" >&2
        echo "  got      $actual" >&2
        rm -f "vendor/${SDK_ARCHIVE}"
        exit 1
    fi

    tar xzf "vendor/${SDK_ARCHIVE}" -C vendor
    rm -f "vendor/${SDK_ARCHIVE}"
fi

# --- the build ---------------------------------------------------------------

BUILD_DIR="build-wclap"
TOOLCHAIN="$ROOT/$WASI_SDK_DIR/share/cmake/wasi-sdk-p1.cmake"

echo "configuring the WebAssembly build"
cmake -S wclap -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" >/dev/null

echo "building"
cmake --build "$BUILD_DIR" --parallel

# --- the bundle --------------------------------------------------------------

VERSION="$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | head -1)"
BUNDLE="$BUILD_DIR/Spiral303.wclap"
DIST_DIR="$ROOT/dist"
DIST_NAME="Spiral303-${VERSION}-wclap"

rm -rf "${DIST_DIR:?}/${DIST_NAME}" "${DIST_DIR:?}/${DIST_NAME}.tar.gz"
mkdir -p "$DIST_DIR"

# The unpacked bundle, for looking at.
cp -R "$BUNDLE" "${DIST_DIR}/${DIST_NAME}"

# The archive: one top directory, plain ustar entries, deterministic bytes
# (script tools/pack_wclap.py). A system `tar czf` on macOS adds AppleDouble
# ._ entries and pax headers, and openDAW then cannot find module.wasm.
python3 tools/pack_wclap.py "$BUNDLE" "${DIST_DIR}/${DIST_NAME}.tar.gz"

# The plinken.org registry artifact: the same files with no top directory. Their
# browser-test-host resolves "<pluginPath>/module.wasm" and does not strip a root
# folder, so a bundle packed for openDAW loads there as an empty wasm module.
REG_NAME="Spiral303-${VERSION}-wclap-registry"
rm -f "${DIST_DIR}/${REG_NAME}.tar.gz"
python3 tools/pack_wclap.py "$BUNDLE" "${DIST_DIR}/${REG_NAME}.tar.gz" --flat

echo "bundle: ${DIST_DIR}/${DIST_NAME}.tar.gz"
echo "registry bundle: ${DIST_DIR}/${REG_NAME}.tar.gz"

# --- the smoke test ---------------------------------------------------------

if [ "$RUN_TESTS" = "1" ] && command -v node >/dev/null 2>&1; then
    echo "the smoke test: the module has to make sound"
    node wclap/tools/wclap-host.mjs "$BUNDLE/module.wasm" --host-half "$BUILD_DIR/host-half.wasm"
else
    echo "the smoke test was skipped"
fi
