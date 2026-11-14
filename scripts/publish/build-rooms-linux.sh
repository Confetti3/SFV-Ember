#!/usr/bin/env bash
# The Linux room-host pair for a release (docs/release/PUBLISHING.md, stage rooms), on any Linux with cargo, g++
# and xxhash.h: WSL on the build PC today. Reads two git archives of the plan's commits from <in>:
#   sfv.tar      (sfv-ember: src scripts data)      library.tar  (Ember library: server rust src)
# and writes to <out>: sfve-room-host, sf4-net, SHA256SUMS and result.txt (KEY=value lines the desktop checks).
#
#   build-rooms-linux.sh <in> <out> <room build id>
#
# The room build id is data/pick-space.json's "build"; the room host must accept it and refuse another. Build
# paths are remapped (rustc --remap-path-prefix, g++ -ffile-prefix-map) and the binaries are scanned for what
# would name this machine.
set -euo pipefail
IN=${1:?usage: build-rooms-linux.sh <in> <out> <room build id>}
OUT=${2:?usage}
BUILD_ID=${3:?usage}
[[ "$BUILD_ID" =~ ^sfv-ember/[0-9]+/[0-9]+\.[0-9]+/[0-9a-f]{16}/layout-[0-9a-f]{16}$ ]] || { echo "Not a room build: $BUILD_ID" >&2; exit 2; }
W="$HOME/sfv-publish-rooms"
rm -rf "$W/src" "$OUT"
mkdir -p "$W/src/lib" "$W/src/sfv" "$OUT"
put() { echo "$1=$2" >> "$OUT/result.txt"; }
: > "$OUT/result.txt"
put BUILD_ID "$BUILD_ID"
put SFV_TAR "$(sha256sum "$IN/sfv.tar" | cut -d' ' -f1)"
put LIBRARY_TAR "$(sha256sum "$IN/library.tar" | cut -d' ' -f1)"
tar -xf "$IN/library.tar" -C "$W/src/lib"
tar -xf "$IN/sfv.tar" -C "$W/src/sfv"
space=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["build"])' "$W/src/sfv/data/pick-space.json")
[ "$space" = "$BUILD_ID" ] || { echo "data/pick-space.json names $space, not $BUILD_ID" >&2; put STATUS wrong-pick-space; exit 3; }

CARGO_HOME_DIR="${CARGO_HOME:-$HOME/.cargo}"
RUSTUP_HOME_DIR="${RUSTUP_HOME:-$HOME/.rustup}"
# Later rustc rules win: the broad one (home) first.
export RUSTFLAGS="--remap-path-prefix=$HOME=/home-remapped --remap-path-prefix=$CARGO_HOME_DIR=/cargo --remap-path-prefix=$RUSTUP_HOME_DIR=/rustup --remap-path-prefix=$W/src/lib=/src --remap-path-prefix=$W/target=/target ${RUSTFLAGS:-}"
export CXXFLAGS="-ffile-prefix-map=$W/src=/src -ffile-prefix-map=$HOME=/home-remapped ${CXXFLAGS:-}"
export CARGO_TARGET_DIR="$W/target"
echo "== sf4-net"
(cd "$W/src/lib/rust/sf4-net" && cargo build --profile server --locked -j "${JOBS:-8}")
cp "$CARGO_TARGET_DIR/server/sf4-net" "$OUT/sf4-net"
if ! echo '#include <xxhash.h>' | g++ -std=c++17 -x c++ -fsyntax-only - 2>/dev/null; then
    for d in "${XXHASH_INCLUDE:-}" "$HOME/sfvrooms/xxhash/usr/include"; do
        if [ -n "$d" ] && [ -f "$d/xxhash.h" ]; then export XXHASH_INCLUDE="$d"; break; fi
    done
fi
echo "== room host"
(cd "$W/src" && bash sfv/scripts/build-room-host-linux.sh lib "$OUT/sfve-room-host")
cd "$OUT"
line='{"room_id":"0123456789abcdef0123456789abcdef","name":"check","capacity":2,"build_id":"%s","bridge_id":"brg_check","ticket_key":"key","ticket_kid":"kid","helper":"/nonexistent","game":"sfv"}'
answer() { printf "$line\n" "$1" | timeout 10 ./sfve-room-host 2>/dev/null | grep -o '"reason":"[a-z_]*"' | head -1 | sed 's/.*:"\(.*\)"/\1/' || true; }
put ANSWER_OWN "$(answer "$BUILD_ID")"
put ANSWER_OTHER "$(answer sfv-ember/0/0.0/0000000000000000/layout-0000000000000000)"
embedded=$(strings -a sfve-room-host | grep -o 'sfv-ember/[0-9]*/[0-9.]*/[0-9a-f]*/layout-[0-9a-f]*' | sort -u | tr '\n' ' ' | sed 's/ $//')
put EMBEDDED "$embedded"
count() { grep -a -o -F -- "$1" "$2" | wc -l | tr -d ' '; }
for f in sfve-room-host sf4-net; do
    k=$(echo "$f" | tr 'a-z-' 'A-Z_')
    n=0
    for needle in /home/ /mnt/ /root/ .cargo/ .rustup/ sfv-publish-rooms "$USER" "$(hostname)"; do
        [ ${#needle} -ge 3 ] || continue
        n=$((n + $(count "$needle" "$f")))
    done
    put "PATHS_$k" "$n"
done
ldd sfve-room-host | awk '{print $1}' > ldd.txt || true
sha256sum sfve-room-host sf4-net > SHA256SUMS
cat SHA256SUMS
put STATUS ok
echo "== done"
