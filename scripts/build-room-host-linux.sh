#!/usr/bin/env bash
# Builds SFV's room host on Linux (docs/design/EMBER_LIBRARY.md section 24):
# the Ember library's room host (server/roomhost/build-linux.sh) with SFV's
# catalog in place of SF4's, judging picks by the pick space the game exported
# (data/pick-space.json, embedded here).
#
#   build-room-host-linux.sh <Ember library root> [<output file>]
#
# <output file> defaults to ./sfve-room-host. Needs what build-linux.sh needs,
# cmake 3.20 or later (it embeds the pick space), and xxhash.h (Debian and Ubuntu: libxxhash-dev, or set XXHASH_INCLUDE to a
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
# The pick space as bytes, by the helper the Windows build embeds it with
# (cmake/EmbedTextFile.cmake: it refuses a byte above 0x7F).
cmake -DSOURCE="$SFV/data/pick-space.json" -DOUTPUT="$GEN/SfvHostPickSpace.cxx" \
    -DNAME_SPACE=sfve::roomhost -DSYMBOL=kPickSpaceJson -P "$SFV/cmake/EmbedTextFile.cmake"

CATALOG_SOURCES="$SFV/src/roomhost/SfvHostCatalog.cxx $SFV/src/ember/SfvPickSpace.cxx $SFV/src/ember/SfvRoomBuild.cxx $GEN/SfvHostPickSpace.cxx" \
CXXFLAGS="-I$SFV/src -I$EMBER/src -I$EMBER/server/roomhost $XXHASH_FLAGS ${CXXFLAGS:-}" \
    bash "$EMBER/server/roomhost/build-linux.sh" "$EMBER" "$OUT"
