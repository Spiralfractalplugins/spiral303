#!/usr/bin/env bash
# Build Spiral303 for Windows (MSVC under Wine) and Linux from a macOS/Linux host,
# without a matching machine. Requires Docker (or OrbStack); on Apple Silicon the
# image is amd64 and runs under Rosetta, so the first build is slow.
#
#   ./tools/windows-cross-build/cross-build.sh            # both platforms
#   ./tools/windows-cross-build/cross-build.sh windows    # Windows only
#   ./tools/windows-cross-build/cross-build.sh linux      # Linux only
#
# The image and its /work volume are kept for repeat builds.
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TOOLS="$REPO/tools/windows-cross-build"

# On macOS hosts OrbStack is the expected docker provider (lighter, what the
# docs and dist workflow assume) - pick it whenever its context is present.
if command -v docker > /dev/null 2>&1 && docker context inspect orbstack > /dev/null 2>&1; then
    export DOCKER_CONTEXT=orbstack
fi
IMAGE=${IMAGE:-spiral303-msvc:1}
CONTAINER=${CONTAINER:-spiral303-msvc}
WORK_VOLUME=${WORK_VOLUME:-spiral303-msvc-work}
TARGETS=${*:-windows linux}

if [ ! -d "$REPO/vendor/JUCE/modules" ]; then
    echo "JUCE is missing - run:" >&2
    echo "  git clone --depth 1 --branch 8.0.14 https://github.com/juce-framework/JUCE.git vendor/JUCE" >&2
    exit 1
fi

# The image layer that downloads MSVC is expensive; rebuild only on demand.
if [ -n "${REBUILD:-}" ] || [ -z "$(docker images -q "$IMAGE")" ]; then
    docker build --platform linux/amd64 -t "$IMAGE" -f "$TOOLS/Dockerfile" "$TOOLS"
fi

# Long-lived container: the wine prefix, /work and the MSVC download survive rebuilds.
if [ -z "$(docker ps -q -a -f name="^${CONTAINER}$")" ]; then
    docker volume create "$WORK_VOLUME" > /dev/null
    docker run -d --name "$CONTAINER" --platform linux/amd64 \
        -v "$WORK_VOLUME:/work" -v "$REPO:/src:ro" \
        --entrypoint sleep "$IMAGE" infinity > /dev/null
fi
docker start "$CONTAINER" > /dev/null

# Copied in rather than bind-mounted, so the container works regardless of which
# host directory it was originally started with.
docker exec "$CONTAINER" mkdir -p /work/tc
docker cp "$TOOLS/." "$CONTAINER:/work/tc/"

for target in $TARGETS; do
    echo "=== building $target ==="
    docker exec "$CONTAINER" rm -rf "/work/out/$target"
    docker exec -e TC=/work/tc "$CONTAINER" bash "/work/tc/build-$target.sh"
    rm -rf "${REPO:?}/build/$target"
    mkdir -p "$REPO/build/$target"
    docker cp "$CONTAINER:/work/out/$target/." "$REPO/build/$target/"
done

find "$REPO/build" -maxdepth 4 \( -name 'Spiral303.vst3' -o -name 'Spiral303.lv2' -o -name 'Spiral303*.exe' \
    -o -name 'Spiral303_Standalone' -o -name 'Spiral303Tests' \)
