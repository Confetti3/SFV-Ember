#!/usr/bin/env bash
# Builds SFV's room host on Linux (docs/design/EMBER_LIBRARY.md section 24):
# the Ember library's room host (server/roomhost/build-linux.sh) with SFV's
# catalog in place of SF4's, judging picks by the pick space the game exported
# (data/pick-space.json, embedded here).
#
#   build-room-host-linux.sh <Ember library root> [<output file>]
#
# <output file> defaults to ./sfve-room-host. Needs what build-linux.sh needs,
# and xxhash.h (Debian and Ubuntu: libxxhash-dev, or set XXHASH_INCLUDE to a
# folder holding it; only the header is used).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SFV="$(cd "$HERE/.." && pwd)"
EMBER="$(cd "${1:?usage: build-room-host-linux.sh <Ember library root> [<output file>]}" && pwd)"
OUT="${2:-$PWD/sfve-room-host}"
CXX="${CXX:-g++}"
XXHASH_FLAGS=""
if [ -n "${XXHASH_INCLUDE:-}" ]; then XXHASH_FLAGS="-I$XXHASH_INCLUDE"; fi
# shellcheck disable=SC2086
if ! echo '#include <xxhash.h>' | "$CXX" -std=c++17 $XXHASH_FLAGS -x c++ -fsyntax-only - 2>/dev/null; then
    echo "Missing header <xxhash.h>: install libxxhash-dev or set XXHASH_INCLUDE." >&2
    exit 1
fi

GEN="$(mktemp -d "${TMPDIR:-/tmp}/sfve-room-host.XXXXXX")"
trap 'rm -rf "$GEN"' EXIT
# The pick space as bytes, as the Windows build embeds it (CMakeLists.txt).
{
    echo '// Generated from data/pick-space.json by scripts/build-room-host-linux.sh.'
    echo 'namespace sfve { namespace roomhost {'
    echo 'static const char kBytes[] = {'
    od -An -tx1 -v "$SFV/data/pick-space.json" | sed -E 's/ ?([0-9a-f]{2})/0x\1,/g'
    echo '0};'
    echo 'extern const char* const kPickSpaceJson = kBytes;'
    echo '} }'
} > "$GEN/SfvHostPickSpace.cxx"

CATALOG_SOURCES="$SFV/src/roomhost/SfvHostCatalog.cxx $SFV/src/ember/SfvPickSpace.cxx $SFV/src/ember/SfvRoomBuild.cxx $GEN/SfvHostPickSpace.cxx" \
CXXFLAGS="-I$SFV/src -I$EMBER/src -I$EMBER/server/roomhost $XXHASH_FLAGS ${CXXFLAGS:-}" \
    bash "$EMBER/server/roomhost/build-linux.sh" "$EMBER" "$OUT"
