"""sfvelib: a copy's log: where it is, setting an old one aside, and what it proves (checkpoints, desyncs)."""

from __future__ import annotations

import re
import time
from pathlib import Path

from sfvelib_config import LOG_DIR


def log_path(index: int) -> Path:
    name = "sfv_ember.log" if index == 0 else f"sfv_ember.{index}.log"
    return LOG_DIR / name


SET_ASIDE_KEPT = 5
# How long a log still held by an exiting copy is waited for before giving up.
SET_ASIDE_WAIT_S = 10.0


def set_aside(path: Path) -> bool:
    """Moves a log a test run is about to start afresh to
    <name>.before-test-<time>.log beside it, so a run never loses a player's
    earlier log (the game's or the launcher's), and keeps the newest
    SET_ASIDE_KEPT of those. True when the path is free for the run: it was
    moved or never there; False when it could not be moved (it is open)."""
    if not path.exists():
        return True
    # Stamped to the millisecond, so the names sort as the logs were set aside
    # and the pruning below keeps the newest; a name already taken waits for
    # the next millisecond rather than overwriting it.
    while True:
        now = time.time()
        stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime(now)) + f"-{int(now * 1000) % 1000:03d}"
        target = path.with_name(f"{path.stem}.before-test-{stamp}{path.suffix}")
        if not target.exists():
            break
        time.sleep(0.002)
    # A copy a run has just ended can hold its log for a moment while it exits.
    deadline = time.monotonic() + SET_ASIDE_WAIT_S
    while True:
        try:
            path.replace(target)
            break
        except OSError:
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.25)
    earlier = sorted(path.parent.glob(f"{path.stem}.before-test-*{path.suffix}"))
    for old in earlier[:-SET_ASIDE_KEPT]:
        try:
            old.unlink()
        except OSError:
            pass
    return True


def tail(index: int, lines: int = 40) -> list[str]:
    path = log_path(index)
    if not path.exists():
        return []
    return path.read_text(errors="replace").splitlines()[-lines:]


def count_log(index: int, text: str) -> int:
    """How many of a copy's log lines hold `text`."""
    path = log_path(index)
    return sum(text in line for line in path.read_text(errors="replace").splitlines()) if path.exists() else 0


# --- what a copy's log proves ---------------------------------------------------
#
# The one reading of the mod's log for every Python runner (the PowerShell
# runners' twin is verdict.ps1: same patterns, same keys). A runner passes only
# on evidence: checkpoints both copies logged and found equal, and no desync line.

# SfvVerification / RollbackSession log "checkpoint: frame F hash H" (H 16 hex digits).
CHECKPOINT_LINE = re.compile(r"checkpoint: frame (\d+) hash ([0-9a-f]+)")
# A frame this far below the previous checkpoint's starts the next game of the log.
GAME_GAP_FRAMES = 600
# A desync's lines ("desync: checkpoint F is H here and H at the other player", its
# "desync: checkpoint F region R, N bytes, hash H" lines, a load "mismatch"), but not the
# report writer's own "desync report:" lines nor a start state's repair. Case-insensitive
# like PowerShell's Select-String; written as a string so a relay can take it as a pattern.
DESYNC_PATTERN = r"(?i)^(?!.*desync report)(?!.*start state).*(desync|mismatch)"
_DESYNC_LINE = re.compile(DESYNC_PATTERN)
_REGION_LINE = re.compile(r"desync: checkpoint (\d+) region (.+), (\d+) bytes, hash ([0-9a-f]+)")


def log_lines(index: int) -> list[str]:
    """All the lines of a copy's log; none when it was never written."""
    path = log_path(index)
    return path.read_text(errors="replace").splitlines() if path.exists() else []


def parse_checkpoints(lines) -> dict[tuple[int, int], str]:
    """The checkpoints a log holds, by (game, frame); the last line of a key wins (a replay logs
    a checkpoint again). A frame far below the last one (GAME_GAP_FRAMES) starts the next game."""
    found: dict[tuple[int, int], str] = {}
    game, previous = 0, -1
    for line in lines:
        m = CHECKPOINT_LINE.search(line)
        if m:
            frame = int(m.group(1))
            if frame < previous - GAME_GAP_FRAMES:
                game += 1
            previous = frame
            found[(game, frame)] = m.group(2)
    return found


def compare_checkpoints(first: dict, second: dict) -> tuple[int, int]:
    """(compared, differing): the checkpoints (parse_checkpoints) both copies logged, and how many
    of those have two hashes. A run with 0 compared proved nothing."""
    common = [key for key in first if key in second]
    return len(common), sum(first[key] != second[key] for key in common)


def desync_lines(lines) -> list[str]:
    return [line for line in lines if _DESYNC_LINE.search(line)]


def count_desyncs(index: int) -> int:
    """How many of a copy's log lines are desync lines (DESYNC_PATTERN)."""
    return len(desync_lines(log_lines(index)))


def region_hashes(lines, checkpoint: int) -> list[tuple[str, int, str]]:
    """(name, size, hash) of each region a log's desync report names at `checkpoint`."""
    return [(m.group(2), int(m.group(3)), m.group(4)) for m in map(_REGION_LINE.search, lines)
            if m and int(m.group(1)) == checkpoint]


def unmatched_regions(here: list[tuple[str, int, str]], there: list[tuple[str, int, str]]):
    """The regions of two copies that have no partner, as (only_here, only_there) lists of
    (name, size, hash). Regions are paired as a multiset of (size, hash), never by name: a run's
    `game.N` name is its place in that process's region list, which two processes number
    differently, so a name pairing reports about 150 false differences. A (size, hash) with
    more copies on one side leaves the surplus unmatched, with the first names that side logged."""
    from collections import Counter
    count_here, count_there = Counter((size, h) for _, size, h in here), Counter((size, h) for _, size, h in there)

    def surplus(regions, mine, theirs):
        spare = mine - theirs
        out = []
        for name, size, h in regions:
            if spare[(size, h)] > 0:
                spare[(size, h)] -= 1
                out.append((name, size, h))
        return out
    return surplus(here, count_here, count_there), surplus(there, count_there, count_here)
