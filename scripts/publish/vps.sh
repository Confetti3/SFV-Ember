#!/usr/bin/env bash
# The pipeline's steps on the VPS, as the operator's own account and never as root (docs/release/PUBLISHING.md).
# The desktop pipes this file over SSH; nothing is left behind but the staged kit:
#   ssh vps 'bash -s -- state   <stage folder> <label> <SHA-256 of SHA256SUMS>'   < vps.sh
#   ssh vps 'bash -s -- install <stage folder> <label> <SHA-256 of SHA256SUMS>'   < vps.sh
#   ssh vps 'bash -s -- check   <room build> <room host SHA-256> <sf4-net SHA-256> <room library>' < vps.sh
# state    whether ~/<stage folder> is that kit already (RESULT staged / not-staged)
# install  ~/<stage folder>.incoming-<label>.tar (scp'd before) unpacked and checked against its SHA256SUMS, the
#          current ~/<stage folder> kept as ~/<stage folder>.before-<label>, the kit moved into its place
# check    read-only: the room build's folder under the room library holds those binaries, its room host accepts
#          its own build, and the supervisor's config.json offers it (RESULT offered); when that file is root-only,
#          RESULT installed-config-unreadable (the owner's sudo upgrade.sh check says the rest)
set -euo pipefail
if [ "$(id -u)" -eq 0 ]; then echo "RESULT refused-root"; exit 2; fi
mode=${1:-}
sums_ok() {
    [ -f "$1/SHA256SUMS" ] && [ "$(sha256sum "$1/SHA256SUMS" | cut -d' ' -f1)" = "$2" ] &&
        (cd "$1" && sha256sum --quiet --strict -c SHA256SUMS) >/dev/null 2>&1
}
case "$mode" in
state)
    if sums_ok "$HOME/$2" "$4"; then echo "RESULT staged"; else echo "RESULT not-staged"; fi
    ;;
install)
    name=$2 label=$3 want=$4
    case "$name$label" in */*|*..*) echo "RESULT bad-name"; exit 2 ;; esac
    stage=$HOME/$name tarball=$HOME/$name.incoming-$label.tar incoming=$HOME/$name.incoming-$label keep=$HOME/$name.before-$label
    if sums_ok "$stage" "$want"; then echo "RESULT staged"; exit 0; fi
    [ -f "$tarball" ] || { echo "RESULT no-upload"; exit 3; }
    rm -rf "$incoming"
    mkdir -p "$incoming"
    tar -xf "$tarball" -C "$incoming"
    sums_ok "$incoming/$name" "$want" || { echo "RESULT upload-differs"; exit 4; }
    for f in upgrade.sh ember-bridge room-build/sfve-room-host room-build/sf4-net ember-short/setup.sh; do
        [ ! -f "$incoming/$name/$f" ] || chmod 0755 "$incoming/$name/$f"
    done
    find "$incoming/$name/bridge-deploy" -maxdepth 1 -name '*.sh' -exec chmod 0755 {} + 2>/dev/null || :
    if [ -e "$stage" ]; then
        [ ! -e "$keep" ] || { echo "RESULT keep-exists"; exit 5; }
        mv "$stage" "$keep"
    fi
    mv "$incoming/$name" "$stage"
    rmdir "$incoming"
    rm -f "$tarball"
    sums_ok "$stage" "$want" || { echo "RESULT staged-differs"; exit 6; }
    echo "RESULT staged"
    ;;
check)
    build=$2 host=$3 net=$4 lib=$5
    [[ "$build" =~ ^sfv-ember/[0-9]+/[0-9]+\.[0-9]+/[0-9a-f]{16}/layout-[0-9a-f]{16}$ ]] || { echo "RESULT bad-build"; exit 2; }
    dir="$lib/builds/sfv-$(echo "${build#sfv-ember/}" | tr / -)"
    if [ ! -d "$dir" ]; then echo "RESULT not-installed"; exit 0; fi
    [[ "$host" =~ ^[0-9a-f]{64}$ ]] && [[ "$net" =~ ^[0-9a-f]{64}$ ]] || { echo "RESULT bad-hashes"; exit 2; }
    # The installed pair is compared in every case, before anything else is looked at.
    if [ "$(sha256sum "$dir/sfve-room-host" | cut -d' ' -f1)" != "$host" ] || [ "$(sha256sum "$dir/sf4-net" | cut -d' ' -f1)" != "$net" ]; then
        echo "RESULT other-binaries"; exit 0
    fi
    line='{"room_id":"0123456789abcdef0123456789abcdef","name":"check","capacity":2,"build_id":"%s","bridge_id":"brg_check","ticket_key":"key","ticket_kid":"kid","helper":"/nonexistent","game":"sfv"}'
    out=$(printf "$line\n" "$build" | timeout 10 "$dir/sfve-room-host" 2>/dev/null | grep -o '"reason":"[a-z_]*"' | head -1 || true)
    if [ "$out" != '"reason":"helper_unavailable"' ]; then echo "RESULT self-check-failed"; exit 0; fi
    config=/etc/ember-rooms/config.json
    if [ -r "$config" ]; then
        if python3 -c '
import json, sys
entry = (json.load(open(sys.argv[1])).get("builds") or {}).get(sys.argv[2]) or {}
ok = entry.get("room_host") == sys.argv[3] + "/sfve-room-host" and entry.get("helper") == sys.argv[3] + "/sf4-net" and entry.get("game") == "sfv"
sys.exit(0 if ok else 1)' "$config" "$build" "$dir"; then echo "RESULT offered"; else echo "RESULT not-offered"; fi
    else
        echo "RESULT installed-config-unreadable"
    fi
    ;;
*)
    sed -n '2,15p' "$0" 2>/dev/null || :
    echo "RESULT usage"; exit 2
    ;;
esac
