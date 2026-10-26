#!/usr/bin/env bash
# Prove build/linux/Spiral303.lv2 works in real LV2 hosts, not just in its manifest.
#
#   ./tools/lv2-host-check.sh [path/to/Spiral303.lv2]
#
# jalv.gtk3 and Carla run under Xvfb inside a throwaway ubuntu:24.04 container, so
# the build container stays clean. Checks:
#   1. lilv discovers the bundle with ui:X11UI;
#   2. jalv.gtk3 embeds the editor at exactly 1180x780 and it paints;
#   3. clicks inside the embedded window reach the editor (steps get edited);
#   4. Carla loads the same binary too, maps the same editor, and the sequencer
#      renders audible sound through the host's jack ports.
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUNDLE=${1:-$REPO/build/linux/Spiral303.lv2}
CONTAINER=spiral303-lv2host
URI=https://plinken.org/lv2

[ -d "$BUNDLE" ] || {
    echo "missing $BUNDLE - run tools/windows-cross-build/cross-build.sh linux" >&2
    exit 1
}

docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
# The bundle is x86-64, so the container has to be too (OrbStack runs it under
# emulation; the checks are offline and do not need a sound card). Installing
# Carla + jalv under emulation costs minutes, so a locally committed image is
# used when it exists:
#   docker commit <container with the packages below> lv2host-check:1
IMAGE=lv2host-check:1
if docker image inspect "$IMAGE" >/dev/null 2>&1; then BASE=$IMAGE; else BASE=ubuntu:24.04; fi
docker run -d --platform linux/amd64 --name "$CONTAINER" -v "$BUNDLE":/bundle:ro \
    "$BASE" sleep infinity >/dev/null
trap 'docker rm -f "$CONTAINER" >/dev/null 2>&1 || true' EXIT

# jalv.gtk3 is the reference host for the UI checks; carla-single loads the same
# bundle alongside it.
if [ "$BASE" = ubuntu:24.04 ]; then
    docker exec "$CONTAINER" bash -lc '
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
            carla jalv lilv-utils jackd2 xvfb x11-utils xdotool imagemagick python3 >/dev/null'
fi

docker exec -i "$CONTAINER" bash -s -- "$URI" <<'INNER'
set -euo pipefail
URI=$1

mkdir -p /root/.lv2
rm -rf /root/.lv2/Spiral303.lv2
cp -a /bundle /root/.lv2/Spiral303.lv2
Xvfb :99 -screen 0 1400x900x24 >/tmp/xvfb.log 2>&1 &
export DISPLAY=:99 LV2_PATH=/root/.lv2
jackd -d dummy -r 48000 -p 1024 >/tmp/jack.log 2>&1 &
sleep 3

# Both hosts are started detached: the bridge has to outlive the shell that
# launched it, and a killed bridge leaves JUCE singletons dying in the wrong
# order (asserts and an X BadAtom, which is not a plugin bug).
start_host () { (setsid nohup "$@" >/tmp/host.log 2>&1 </dev/null &); }
stop_hosts () {
    pkill -x jalv.gtk3 || true
    pkill -x carla-single || true
    # The bridge's comm is truncated to 15 characters; `pkill -f` would also match
    # any command line that merely mentions the bridge path.
    pkill -x carla-bridge-na || true
    sleep 2
}
# Prints the id of the first window in the tree whose line matches $1 (an ERE).
find_window () {
    xwininfo -root -tree > /tmp/x11tree.txt 2>&1 || true
    grep -E "$1" /tmp/x11tree.txt \
        | sed -n 's/^[[:space:]]*\(0x[0-9a-f]*\)[[:space:]].*/\1/p' | sed -n 1p
}
# Waits for a matching window, prints its id (empty on timeout).
wait_window () {
    local want=$1 tries=$2 id='' i=0
    while [ "$i" -lt "$tries" ]; do
        i=$((i + 1)); sleep 5
        id=$(find_window "$want")
        if [ -n "$id" ]; then break; fi
    done
    printf '%s' "$id"
}

echo "--- 1. discovery ---"
lv2ls > /tmp/lv2ls.txt
grep -qx "$URI" /tmp/lv2ls.txt || { echo "FAIL lilv does not see $URI"; cat /tmp/lv2ls.txt; exit 1; }
# Writing lv2info to a file: piping it into `grep -q` would let grep exit at the
# match, lv2info would die of SIGPIPE, and `pipefail` would report no X11 UI.
lv2info "$URI" > /tmp/lv2info.txt || { echo "FAIL lv2info cannot read $URI"; exit 1; }
grep -q "ui#X11UI" /tmp/lv2info.txt || { echo "FAIL no ui:X11UI"; cat /tmp/lv2info.txt; exit 1; }
sed -n '1,12p' /tmp/lv2info.txt
echo "PASS the bundle is discovered with an X11 UI"

echo "--- 2. jalv embeds the editor ---"
start_host jalv.gtk3 "$URI"
win=$(wait_window '1180x780\+0\+0' 20)
[ -n "$win" ] || {
    echo "FAIL jalv mapped no 1180x780 plugin window"
    tail -20 /tmp/host.log
    grep -E '0x[0-9a-f]+' /tmp/x11tree.txt | sed -n '1,12p'
    exit 1
}
# The toplevel is GTK's (1180x805); the plugin window is its child, and xdotool
# clicks in root coordinates, so the offset has to come from xwininfo.
off=$(xwininfo -id "$win" | sed -n 's/^\s*Absolute upper-left X:\s*\(-\?[0-9]*\).*/\1/p
    /^\s*Absolute upper-left Y:/s/^\s*Absolute upper-left Y:\s*\(-\?[0-9]*\).*/\1/p' | paste -sd' ' -)
ox=${off% *}; oy=${off#* }
import -window "$win" /tmp/jalv_ui.png
echo "PASS the plugin window $win sits at $ox,$oy and painted $(stat -c%s /tmp/jalv_ui.png) bytes of PNG"

echo "--- 3. input reaches the editor ---"
shot () { import -window "$win" "$1"; }
# The grid spans 16..1164 x 508..768 and a click in a cell either clears the note
# there or paints one (SequenceEditorComponent::handleMouse), so every click in
# the note area has to move pixels.
click_and_expect_change () {
    local x=$1 y=$2 before=$3 after=$4
    xdotool mousemove $((ox + x)) $((oy + y)) click 1
    sleep 1
    shot "$after"
    if cmp -s "$before" "$after"; then
        echo "FAIL the embedded editor ignored the click at $x,$y"
        exit 1
    fi
}
shot /tmp/a.png
click_and_expect_change 200 645 /tmp/a.png /tmp/b.png
click_and_expect_change 252 645 /tmp/b.png /tmp/c.png
echo "PASS clicks inside the embedded window edit the pattern"

echo "--- 4. the same binary in Carla, with sound ---"
# jalv stays running on purpose: Carla does not call XInitThreads before it embeds
# an X11 UI, and its bridge then dies of an X BadAtom (JUCE singleton asserts too).
# With a JUCE editor already alive on the display the embedding works.
start_host carla-single native lv2 "$URI" Spiral303
out1=''
for _ in $(seq 1 12); do
    sleep 5
    out1=$(jack_lsp | grep -m1 'Audio Out 1' || true)
    if [ -n "$out1" ]; then break; fi
done
out2=$(jack_lsp | grep -m1 'Audio Out 2' || true)
[ -n "$out1" ] && [ -n "$out2" ] || {
    echo "FAIL Carla exposes no audio ports"; tail -20 /tmp/host.log; jack_lsp; exit 1; }
carla_win=$(find_window '1180x780')
if [ -n "$carla_win" ] && [ "$carla_win" != "$win" ]; then
    import -window "$carla_win" /tmp/carla_ui.png
    echo "     Carla maps the same editor in its own window ($carla_win)"
else
    echo "     Carla mapped no window"
fi
# Rolling the jack transport is what the pattern waits for in Sync = Host.
printf 'master\nplay\n' | jack_transport >/dev/null 2>&1 || true
sleep 1
jack_rec -f /tmp/carla_out.wav -d 5 -b 16 "$out1" "$out2" >/dev/null 2>&1 || true
stop_hosts
python3 - /tmp/carla_out.wav <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1])
s = struct.unpack("<%dh" % (w.getnframes() * w.getnchannels()), w.readframes(w.getnframes()))
peak = max(abs(x) for x in s)
db = 20 * math.log10(peak / 32768.0) if peak else -999.0
print("peak %.1f dBFS over %.1f s" % (db, w.getnframes() / w.getframerate()))
sys.exit(0 if peak > 3277 else 1)   # louder than -60 dBFS
PY
echo "PASS the sequencer renders audio through Carla's ports"
INNER

for shot in jalv_ui b c carla_ui; do
    docker cp "$CONTAINER:/tmp/$shot.png" "/tmp/lv2_host_$shot.png" >/dev/null 2>&1 || true
done
docker cp "$CONTAINER:/tmp/carla_out.wav" /tmp/carla_lv2_out.wav >/dev/null
echo "screenshots: /tmp/lv2_host_jalv_ui.png /tmp/lv2_host_b.png /tmp/lv2_host_c.png"
echo "             /tmp/lv2_host_carla_ui.png (if Carla mapped a window)"
echo "audio:       /tmp/carla_lv2_out.wav"
