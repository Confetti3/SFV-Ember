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
