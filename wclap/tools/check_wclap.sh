#!/usr/bin/env bash
# Checks the WebAssembly build: it makes sound, it sounds like the native one,
# and its interface draws itself.
#
#   wclap/tools/check_wclap.sh
#
# The build itself comes from tools/build_wclap.sh.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build-wclap"
MODULE="$BUILD_DIR/Spiral303.wclap/module.wasm"
HOST_HALF="$BUILD_DIR/host-half.wasm"

if [ ! -f "$MODULE" ]; then
    echo "the WebAssembly build is missing - run tools/build_wclap.sh first" >&2
    exit 1
fi

command -v node >/dev/null 2>&1 || { echo "node is needed for the checks" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The newest of the test binaries: a stale one does not know how to dump.
TEST_BINARY=""
for candidate in build/Spiral303Tests_artefacts/Release/Spiral303Tests \
                 build/Spiral303Tests_artefacts/Debug/Spiral303Tests; do
    if [ -x "$candidate" ] && { [ -z "$TEST_BINARY" ] || [ "$candidate" -nt "$TEST_BINARY" ]; }; then
        TEST_BINARY="$candidate"
    fi
done

echo "== the module makes sound =="
node wclap/tools/wclap-host.mjs "$MODULE" --host-half "$HOST_HALF" | tail -3

echo
echo "== the interface is served and answers =="
node wclap/tools/wclap-host.mjs "$MODULE" --host-half "$HOST_HALF" --webview | tail -3

echo
echo "== the host's transport drives the clock, and the page's edits reach the host =="
node wclap/tools/wclap-host.mjs "$MODULE" --host-half "$HOST_HALF" --transport | tail -2

echo
echo "== the extensions, the ports the host builds its buffers from, and the state =="
# A host that reads a function pointer without checking it for null traps, and
# the error it reports is a bare address in the module's memory (openDAW:
# "null function pointer at 117660"). The check walks every extension.
# Nothing is truncated here: the line that explains a failure is the one a tail
# would cut.
node wclap/tools/wclap-host.mjs "$MODULE" --host-half "$HOST_HALF" --api --state-out "$WORK/wasm_state.bin"

echo
echo "== the page around the canvas =="
# The letterbox, the fallback message, the field ring and the bundle contents:
# none of them is drawn by the plugin, so none of them shows up in a canvas check.
node wclap/tools/check_page_css.mjs

echo
echo "== the interface draws itself =="
node wclap/tools/dump_state.mjs "$MODULE" "$HOST_HALF" "$WORK/state.json"
node wclap/tools/check_ui.mjs "$WORK/state.json"

ARCHIVE="$(ls -t dist/Spiral303-*-wclap.tar.gz | head -1)"

echo
echo "== the archive parses the way openDAW imports it =="
# macOS tar writes AppleDouble ._ entries and pax headers; the importer then
# cannot find module.wasm and rejects the bundle ("is not a WebCLAP bundle").
node wclap/tools/check_bundle_tar.mjs "$BUILD_DIR/Spiral303.wclap" "$ARCHIVE"

if [ -z "$TEST_BINARY" ]; then
    echo
    echo "the native test binary is missing - the rest of the checks are skipped"
    exit 0
fi

echo
echo "== the state travels from the WebAssembly build into the native one =="
loaded="$(SPIRAL303_LOAD_STATE="$WORK/wasm_state.bin" "$TEST_BINARY" 2>/dev/null | grep '^loaded' || true)"
echo "$loaded"
case "$loaded" in
    *"cutoff=1234.5"*) echo "PASS  the native build reads the state the WebAssembly build wrote" ;;
    *) echo "FAIL  the native build did not read the state"; exit 1 ;;
esac

echo
echo "== the WebAssembly build sounds like the native one =="
SPIRAL303_DUMP_RENDER="$WORK/native.f32" "$TEST_BINARY" >/dev/null
node wclap/tools/wclap-host.mjs "$MODULE" --host-half "$HOST_HALF" --render "$WORK/wasm.f32" >/dev/null
python3 wclap/tools/compare_render.py "$WORK/native.f32" "$WORK/wasm.f32"
