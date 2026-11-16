"""sfvelib: requests to a running copy over its control pipe."""

from __future__ import annotations

import time

from sfvelib_config import SfveError


def pipe_path(instance: int) -> str:
    return rf"\\.\pipe\sfve-{instance}"


def request(instance: int, line: str, timeout_s: float = 5.0, idempotent: bool = False) -> str:
    """Sends one command line to a running copy; returns its reply line.

    The pipe is opened again only while no write has gone through. Once the
    line was written it is never sent again: a command can have taken effect
    (`replay pause` is a toggle), so an empty reply is an error for the caller
    (SfveError) to see. `idempotent` marks a pure read (status, peek): a read
    whose reply came back empty is asked again until the timeout."""
    deadline = time.monotonic() + timeout_s
    while True:
        written = False
        try:
            with open(pipe_path(instance), "r+b", buffering=0) as pipe:
                pipe.write(line.encode() + b"\n")
                written = True
                reply = b""
                while not reply.endswith(b"\n"):
                    chunk = pipe.read(65536)
                    if not chunk:
                        break
                    reply += chunk
                text = reply.decode(errors="replace").strip()
                if text:
                    return text
                # A copy that is closing can accept and then drop the line.
                raise OSError("empty reply")
        except (FileNotFoundError, OSError):
            if written and not idempotent:
                raise SfveError(f"instance {instance}: no reply to {line.split(' ', 1)[0]!r} after it was sent "
                                f"(not sent again: it may have taken effect)") from None
            if time.monotonic() > deadline:
                raise SfveError(f"instance {instance} is not answering on {pipe_path(instance)}")
            time.sleep(0.1)


def request_ok(instance: int, line: str, timeout_s: float = 5.0) -> str:
    """request() for a command that must succeed: a reply that does not start
    with "ok" (a refusal, an error) raises SfveError. Returns the reply."""
    reply = request(instance, line, timeout_s)
    if not reply.startswith("ok"):
        raise SfveError(f"instance {instance}: {line[:80]!r} was refused: {reply[:200]}")
    return reply


def _reply_pid(reply: str) -> int:
    """The game's own process id in a status reply."""
    return int(reply.split('"pid":', 1)[1].split(",", 1)[0])


def status_pid(index: int) -> int:
    reply = request(index, "status", idempotent=True)
    if not reply.startswith("ok "):
        raise SfveError(f"instance {index}: {reply}")
    return _reply_pid(reply)
