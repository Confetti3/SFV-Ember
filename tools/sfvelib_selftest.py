"""Self-test of the shared test-driver module (sfvelib.py): pure checks, no game,
no pipe, no process. Run: python -I tools/sfvelib_selftest.py"""

from __future__ import annotations

import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sfvelib as lib  # noqa: E402

CHECKS: list = []


def check(fn):
    CHECKS.append(fn)
    return fn


class FakePipe(io.RawIOBase):
    """One pipe connection: records the written line, answers `reply`."""

    def __init__(self, log: list, reply: bytes):
        self.log, self.reply = log, reply

    def write(self, data):
        self.log.append(bytes(data))
        return len(data)

    def read(self, n=-1):
        data, self.reply = self.reply, b""
        return data

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def with_pipe(replies: list[bytes | Exception], fn):
    """Runs fn with lib.open answering one connection per entry of `replies` (an
    exception entry fails the open); returns (writes, result_or_exception)."""
    writes: list[bytes] = []
    queue = list(replies)

    def fake_open(path, mode="r", buffering=-1):
        item = queue.pop(0) if queue else FileNotFoundError("no pipe")
        if isinstance(item, Exception):
            raise item
        return FakePipe(writes, item)

    lib.open = fake_open
    try:
        try:
            return writes, fn()
        except lib.SfveError as e:
            return writes, e
    finally:
        del lib.open


@check
def request_never_resends_a_written_line():
    writes, result = with_pipe([b"", b"ok toggled\n"], lambda: lib.request(0, "replay pause", timeout_s=1.0))
    assert isinstance(result, lib.SfveError), result
    assert writes == [b"replay pause\n"], writes  # sent once, not again


@check
def request_retries_while_the_pipe_is_not_there():
    writes, result = with_pipe([FileNotFoundError("x"), b"ok\n"], lambda: lib.request(0, "ping", timeout_s=1.0))
    assert result == "ok" and writes == [b"ping\n"], (result, writes)


@check
def request_resends_a_pure_read():
    writes, result = with_pipe([b"", b'ok {"a":1}\n'], lambda: lib.request(0, "status", timeout_s=1.0, idempotent=True))
    assert result == 'ok {"a":1}' and len(writes) == 2, (result, writes)


@check
def request_ok_refuses_a_refusal():
    _, result = with_pipe([b"error: no such pad\n"], lambda: lib.request_ok(0, "pad 9 A:100"))
    assert isinstance(result, lib.SfveError) and "was refused" in str(result), result
    _, result = with_pipe([b"ok queued\n"], lambda: lib.request_ok(0, "pad 0 A:100"))
    assert result == "ok queued", result


def _log(*frames, hash_of=lambda f: f"{f:016x}"):
    return [f"2026-11-15 12:00:00 core: checkpoint: frame {f} hash {hash_of(f)} (copy 3 us, hash 9 us)" for f in frames]


@check
def checkpoints_are_keyed_by_game_and_frame():
    found = lib.parse_checkpoints(_log(20, 40, 5000, 20, 40))
    assert set(found) == {(0, 20), (0, 40), (0, 5000), (1, 20), (1, 40)}, sorted(found)
    assert lib.parse_checkpoints(["checkpoint: frame 20 hash 0a", "checkpoint: frame 20 hash 0b"]) == {(0, 20): "0b"}  # the last line wins
    assert lib.parse_checkpoints(["nothing here"]) == {}


@check
def zero_compared_checkpoints_is_visible_as_zero():
    one, two = lib.parse_checkpoints(_log(20, 40)), lib.parse_checkpoints([])
    assert lib.compare_checkpoints(one, two) == (0, 0)  # a caller must read 0 compared as a failure
    assert lib.compare_checkpoints(one, lib.parse_checkpoints(_log(20, 40))) == (2, 0)
    differing = lib.parse_checkpoints(_log(20, 40, hash_of=lambda f: "ff" if f == 40 else f"{f:016x}"))
    assert lib.compare_checkpoints(one, differing) == (2, 1)
    other_game = lib.parse_checkpoints(_log(20, 5000, 20))
    assert lib.compare_checkpoints(one, other_game) == (1, 0)  # (0, 20) only: game 1's frame 20 is not game 0's


@check
def one_desync_pattern_for_every_runner():
    yes = ["desync: checkpoint 40 is 00aa here and 00bb at the other player; the fight ends",
           "desync: checkpoint 40 region CharaUnit[0]+0xF8, 64 bytes, hash 00aa",
           "netplay: DESYNC at 40", "rollback: load mismatch at frame 7"]
    no = ["desync report: written as x.state (12 regions)", "start: P1's start state hash 00aa differs from this copy's",
          "desync report: the start state's repair, 3 region(s)", "checkpoint: frame 20 hash 00aa"]
    assert len(lib.desync_lines(yes + no)) == len(yes), lib.desync_lines(yes + no)


@check
def regions_pair_by_size_and_hash_not_by_name():
    here = [("game.12", 64, "aa"), ("game.13", 64, "aa"), ("CharaUnit[0]+0xF8", 200, "bb"), ("game.40", 96, "cc")]
    there = [("game.97", 96, "cc"), ("game.98", 64, "aa"), ("game.99", 64, "aa"), ("CharaUnit[0]+0xF8", 200, "bb")]
    assert lib.unmatched_regions(here, there) == ([], [])  # the same state under other game.N numbers
    only_here, only_there = lib.unmatched_regions(here, there[:2] + [("CharaUnit[0]+0xF8", 200, "dd")])  # one aa short, bb changed
    assert [r[0] for r in only_here] == ["game.12", "CharaUnit[0]+0xF8"] and [r[0] for r in only_there] == ["CharaUnit[0]+0xF8"], (only_here, only_there)


def main() -> int:
    failed = 0
    for fn in CHECKS:
        try:
            fn()
            print(f"ok   {fn.__name__}")
        except AssertionError as e:
            failed += 1
            print(f"FAIL {fn.__name__}: {e}")
    print(f"{len(CHECKS) - failed}/{len(CHECKS)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
