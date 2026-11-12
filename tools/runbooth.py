"""Photo booth capture driver: every SFV fighter look and loadout picture, several game copies at once.

Development builds only (the mod's booth mode, -sfve_mode=booth; the pipe protocol is
docs/design/beta8/booth-protocol.md). Usage and timing: docs/design/beta8/tools.md.

  python -I tools/runbooth.py --phase all --copies 4
  python -I tools/runbooth.py --phase 1 --fighters RYU,KEN --per-fighter 4 --copies 2     (pilot)
  python -I tools/runbooth.py --plan --phase all --copies 4                              (no game)

Each copy is its own game instance (pipe sfve-<index>), booted to the main menu with
-sfve_pads=0 -sfve_background=1, and takes a disjoint slice of the worklist. Per load (two looks,
P1 and P2): launch versus -> wait loaded (a look mismatch skips the load) -> optional walk-in ->
booth hold -> 12 presents -> camera check -> grab A -> scale both fighters away -> 12 presents ->
grab B -> booth leave -> wait for the main menu. Every wait has a timeout; a copy that times out
tries `booth leave`, else it is killed and relaunched (that copy only). After each load the masters
are written (boothpack.make_masters), so they accumulate during the run. Resumable: a look whose
master exists is not captured again.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sfvelib as lib  # noqa: E402

try:
    import numpy as np
    from PIL import Image
    import boothburst as bb
    import boothcamera as bc
    import boothpack as bp
except ImportError as e:  # pragma: no cover
    sys.exit(f"runbooth needs Pillow and numpy ({e}): python -m pip install -r tools/booth-requirements.txt (in a venv)")

VERSION = bp.VERSION
LAUNCH_LOCK = Path(os.environ.get("SFVE_LAUNCH_LOCK", r"D:\sfv-re\sfv-launch.lock"))
LOCK_STALE_S = 45 * 60
LOCK_OWNER = "sfv booth capture (runbooth.py)"
LOADOUT_FRAMES = Path(__file__).resolve().parent / "booth" / "loadout-frames.json"
MAIN_MENU_SCENE = lib.MAIN_MENU_SCENE

# Timeouts (seconds); each wait of a load has one.
BOOT_TIMEOUT_S = 150.0
LOADED_TIMEOUT_S = 120.0
CAMERA_TIMEOUT_S = 20.0
GRAB_TIMEOUT_S = 30.0
PRESENTS_TIMEOUT_S = 30.0
LEAVE_TIMEOUT_S = 60.0
SETTLE_PRESENTS = 12          # protocol: wait 12 presents after a hold and after a scale
LOADED_FRAMES = 60            # SFV reports nothing loading for this many battle frames in a row before the grabs
LOADED_TIMEOUT_GRAB_S = 45.0  # ...or the load fails
STABLE_PRESENTS = 20          # grab A again this many presents later: the same picture once everything has loaded
STABLE_TRIES = 5
LOOK_ATTEMPTS = 2             # a load whose grabs fail the checks is loaded again once
BURST_AFTER_PRESENTS = 3      # a burst grab of a frame just held: a few presents later, the frame drawn
BURST_POLL_S = 0.05           # the burst's waits poll the pipe this often
CAMERA_PEEK = "peek CameraUnit[0]+0x450 0 16"

PHASE_NAMES = {"1": "colour 1 of every costume", "2": "the other colours", "3": "loadout pictures"}


class CaptureError(RuntimeError):
    """A load that failed; the text is the reason for the capture log."""


class Stopped(Exception):
    pass


class GrabCheckError(CaptureError):
    """A load's grabs failed a check (boothpack.check_pair, the held frame changing)."""


# --- the worklist -----------------------------------------------------------------------------

Look = tuple  # (code, costume, colour)


def phase_looks(space: dict, phase: str) -> list[Look]:
    out: list[Look] = []
    for code, fighter in space["fighters"].items():
        for costume, colours in fighter["costumes"].items():
            chosen = colours[:1] if phase == "1" else colours[1:]
            out += [(code, costume, colour) for colour in chosen]
    return out


def parse_looks(text: str, space: dict) -> list[Look]:
    out: list[Look] = []
    for item in text.split(","):
        parts = item.strip().split(":")
        if len(parts) != 3:
            raise SystemExit(f"--looks wants CODE:costume:colour, not {item!r}")
        code, costume, colour = parts[0].upper(), int(parts[1]), int(parts[2])
        if colour not in space["fighters"].get(code, {"costumes": {}})["costumes"].get(costume, []):
            raise SystemExit(f"{item}: not a look of the pick space")
        out.append((code, costume, colour))
    return out


def pair_up(looks: list[Look]) -> list[tuple[Look, Look | None]]:
    """Two looks to a load (P1, P2); the odd one out alone. Consecutive looks are paired, so a
    load is usually two colours of one costume (never the same look twice)."""
    return [(looks[i], looks[i + 1] if i + 1 < len(looks) else None) for i in range(0, len(looks), 2)]


def loadout_todo(store: Path, space: dict, code: str) -> list[tuple[int, str, int]]:
    """(side, "vs"|"vt", choice) still to capture for a fighter: side 0 = choice 1, side 1 = choice 2."""
    fighter = space["fighters"][code]
    todo = []
    for side in (0, 1):
        choice = side + 1
        for prefix in ("vs", "vt"):
            if choice <= fighter[prefix] and not bp.loadout_master_path(store, code, prefix, choice).is_file():
                todo.append((side, prefix, choice))
    return todo


def build_loads(args, space: dict, store: Path) -> list[dict]:
    """The worklist as loads, in capture order. Each load is {"kind": "looks", "looks": (P1, P2)}
    or {"kind": "loadout", "fighter": code}."""
    loads: list[dict] = []
    wanted_fighters = {c.strip().upper() for c in args.fighters.split(",")} if args.fighters else None
    if wanted_fighters:
        bad = wanted_fighters - set(space["fighters"])
        if bad:
            raise SystemExit(f"not in the pick space: {', '.join(sorted(bad))}")

    def limited(looks: list[Look]) -> list[Look]:
        if wanted_fighters:
            looks = [look for look in looks if look[0] in wanted_fighters]
        if args.per_fighter:
            seen: dict[str, int] = {}
            kept = []
            for look in looks:
                if seen.get(look[0], 0) < args.per_fighter:
                    seen[look[0]] = seen.get(look[0], 0) + 1
                    kept.append(look)
            looks = kept
        return looks

    if args.looks_file:
        text = Path(args.looks_file).read_text(encoding="utf-8")
        looks = parse_looks(",".join(line.strip() for line in text.splitlines() if line.strip()), space)
    elif args.looks:
        looks = parse_looks(args.looks, space)
    elif args.retry_failed:
        looks = limited(sorted(bp.failed_looks(store)))
    else:
        looks = []
        for phase in ("1", "2"):
            if args.phase in (phase, "all"):
                looks += limited(phase_looks(space, phase))
    if not args.force:
        looks = [look for look in looks if not bp.master_path(store, *look).is_file()]
    if args.limit:
        looks = looks[: args.limit]
    if args.sides == 1:
        loads += [{"kind": "looks", "looks": (look, None)} for look in looks]
    else:
        loads += [{"kind": "looks", "looks": pair} for pair in pair_up(looks)]
    if args.phase in ("3", "all") and not args.looks and not args.looks_file and not args.retry_failed:
        for code in space["fighters"]:
            if wanted_fighters and code not in wanted_fighters:
                continue
            if args.force or loadout_todo(store, space, code):
                loads.append({"kind": "loadout", "fighter": code})
        if args.limit and args.phase == "3":
            loads = loads[: args.limit]
    return loads


def slice_for(loads: list[dict], copy: int, copies: int) -> list[dict]:
    return loads[copy::copies]


# --- the loadout configuration ------------------------------------------------------------------

def load_loadout_config(path: Path = LOADOUT_FRAMES) -> dict:
    cfg = json.loads(path.read_text(encoding="utf-8"))
    cfg.setdefault("start", 150)
    cfg.setdefault("press", 4)
    cfg.setdefault("gap", 150)
    cfg.setdefault("default", {"vskill": 20, "vtrigger": 45})
    cfg.setdefault("crop", {}).setdefault("height_fraction", 0.85)
    auto = cfg.setdefault("auto", {})
    auto.setdefault("step", 4)
    auto.setdefault("span", 120)
    auto.setdefault("settle_frames", 20)
    cfg.setdefault("fighters", {})
    return cfg


def override_after_press(cfg: dict, code: str, prefix: str, choice: int) -> int | None:
    """The fight frames after the press whose grab is the picture, where loadout-frames.json's
    `fighters` gives one for this move (an override of the automatic choice); else None."""
    key = bp.PART_NAMES[prefix]
    value = cfg["fighters"].get(code, {}).get(key)
    if isinstance(value, dict):
        value = value.get(str(choice))
    return int(value) if value else None


def default_after_press(cfg: dict, prefix: str) -> int:
    """The frames after the press taken when the burst shows nothing the chooser can use."""
    return int(cfg["default"][bp.PART_NAMES[prefix]])


# --- locks, copies ----------------------------------------------------------------------------------

def take_launch_lock() -> None:
    while LAUNCH_LOCK.exists():
        age = time.time() - LAUNCH_LOCK.stat().st_mtime
        if age > LOCK_STALE_S or not lib.running_pids():
            break
        print(f"waiting for the launch lock: {LAUNCH_LOCK.read_text(errors='replace').strip()}", flush=True)
        time.sleep(30)
    try:
        LAUNCH_LOCK.parent.mkdir(parents=True, exist_ok=True)
        LAUNCH_LOCK.write_text(f"{LOCK_OWNER}, {time.strftime('%Y-%m-%dT%H:%M:%S')}\n")
    except OSError as e:
        print(f"note: launch lock not written ({e})", flush=True)


def release_launch_lock() -> None:
    try:
        if LAUNCH_LOCK.read_text(errors="replace").startswith(LOCK_OWNER):
            LAUNCH_LOCK.unlink()
    except OSError:
        pass


def kill_copy(index: int) -> None:
    """Ends one copy this run started (its launcher, game and helpers) and waits for its pipe to go."""
    folder = lib.registry_path()
    entries = lib._registered(folder).get(str(index), [])  # noqa: SLF001 - the library's own registry
    if entries:
        lib._kill_tree(entries)  # noqa: SLF001
    try:
        (folder / f"{index}.json").unlink()
    except OSError:
        pass
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            lib.request(index, "ping", timeout_s=0.5)
        except lib.SfveError:
            return
        time.sleep(0.5)


class Log:
    """Thread-safe capture-log appends and console lines."""

    def __init__(self, path: Path):
        self.path = path
        self.lock = threading.Lock()

    def say(self, text: str) -> None:
        with self.lock:
            print(text, flush=True)

    def failure(self, copy: int, phase: str, look: dict, reason: str, stage: str) -> None:
        entry = {"time": time.strftime("%Y-%m-%dT%H:%M:%S"), "copy": copy, "phase": phase, "look": look,
                 "reason": reason, "stage": stage}
        with self.lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            with open(self.path, "a", encoding="utf-8") as f:
                f.write(json.dumps(entry, ensure_ascii=True) + "\n")
        print(f"FAILED {look}: {reason}", flush=True)


_CAMERA_WARNED = False
BOOT_LOCK = threading.Lock()  # one copy starts at a time: the guard and the loading of several at once starve each other


class Copy:
    def __init__(self, index: int, args, space: dict, store: Path, log: Log, loads: list[dict], stop: threading.Event,
                 post: ThreadPoolExecutor, cfg: dict):
        self.index, self.args, self.space, self.store, self.log = index, args, space, store, log
        self.loads, self.stop, self.post, self.cfg = loads, stop, post, cfg
        self.since_boot = 0
        self.done = 0       # loads captured
        self.attempted = 0  # loads tried
        self.failed = 0     # looks / pictures that failed
        self.grabbed = False  # this load's grabs passed and went to masking
        self.started = 0.0
        self.pending: list = []

    # --- pipe ---

    def status(self) -> dict:
        if self.stop.is_set():
            raise Stopped()
        reply = lib.request(self.index, "status")
        if not reply.startswith("ok "):
            raise CaptureError(f"status: {reply}")
        return json.loads(reply[3:])

    def cmd(self, line: str) -> str:
        if self.stop.is_set():
            raise Stopped()
        reply = lib.request(self.index, line)
        if not reply.startswith("ok"):
            raise CaptureError(f"{line[:60]}: {reply[:200]}")
        return reply

    @staticmethod
    def booth(status: dict) -> dict:
        return status.get("booth") or {}

    def wait_for(self, predicate, timeout_s: float, what: str, poll_s: float = 0.25) -> dict:
        deadline = time.monotonic() + timeout_s
        while True:
            status = self.status()
            if predicate(status):
                return status
            if time.monotonic() > deadline:
                raise CaptureError(f"timeout ({timeout_s:.0f} s) waiting for {what}")
            time.sleep(poll_s)

    def wait_presents(self, n: int = SETTLE_PRESENTS) -> int:
        first = self.booth(self.status()).get("presents", 0)
        status = self.wait_for(lambda s: self.booth(s).get("presents", 0) >= first + n, PRESENTS_TIMEOUT_S, f"{n} presents")
        return self.booth(status).get("fightFrame", -1)

    # --- copy lifecycle ---

    def boot(self) -> None:
        last: Exception | None = None
        for attempt in range(3):
            try:
                with BOOT_LOCK:
                    kill_copy(self.index)
                    lib.launch(self.index, "booth", ["-sfve_pads=0", "-sfve_background=1"] + list(self.args.game_option or []))
                    time.sleep(self.args.stagger)
                lib.boot_to_menu(self.index, timeout=BOOT_TIMEOUT_S, bring_to_front=False)
                self.since_boot = 0
                if not self.args.show_hud:
                    # The game's own switch for its HUD (each HUD part's movies hidden, as a
                    # cinematic hides them): kept by the mod for every battle from now on.
                    self.cmd("booth hudhide on")
                return
            except (lib.SfveError, TimeoutError, OSError) as e:
                last = e
                self.log.say(f"copy {self.index}: boot attempt {attempt + 1} failed: {e}")
                kill_copy(self.index)
        raise CaptureError(f"copy {self.index} cannot boot: {last}")

    def to_menu(self) -> None:
        self.wait_for(lambda s: s.get("scene") == MAIN_MENU_SCENE, LEAVE_TIMEOUT_S, "the main menu")
        # The stage just left is still being unloaded: a launch on the same stage straight away
        # never finishes loading (the laptop pilot 2026-11-12: 1-2 s after the leave it hung,
        # 4 s and 8 s after it loaded three in a row).
        time.sleep(self.args.menu_settle)

    def recover(self) -> None:
        """After a failed load: `booth leave` and the menu, else kill and relaunch this copy."""
        try:
            self.cmd("booth leave")
            self.wait_for(lambda s: s.get("scene") == MAIN_MENU_SCENE, 30, "the main menu after a recovery leave")
        except Stopped:
            raise
        except (CaptureError, lib.SfveError, ValueError):
            self.log.say(f"copy {self.index}: leave failed, relaunching this copy")
            self.boot()

    # --- steps of a load ---

    def start_battle(self, p1: str, p2: str) -> None:
        for attempt in (1, 2):
            b0 = self.booth(self.status()).get("battle", 0)
            self.cmd(f"launch {self.args.target} {p1} {p2} {self.args.stage} 1 inf")

            def settled(s: dict) -> bool:
                launch = s.get("launch") or {}
                loaded = launch.get("loaded") and self.booth(s).get("battle", 0) != b0
                return bool(loaded or launch.get("failure") or launch.get("state") == "failed")

            try:
                status = self.wait_for(settled, self.args.load_stuck, "the launch to load")
                break
            except CaptureError:
                # A load of the stage just left, while the game still unloads it, never
                # finishes (laptop 2026-11-12, more often with several copies): this copy is
                # started again and the load tried once more.
                if attempt == 2:
                    raise
                self.log.say(f"copy {self.index}: the load of {p1} / {p2} is stuck; relaunching this copy and trying again")
                self.boot()
        launch = status.get("launch") or {}
        if (launch.get("failure") or launch.get("state") == "failed") and not launch.get("loaded"):
            raise CaptureError(f"launch failed: {launch.get('failure') or 'no reason given'}")
        if launch.get("mismatch"):
            raise CaptureError(f"look mismatch: asked {p1} / {p2}, the game has {json.dumps(launch.get('looks'))}")
        for hide in self.args.hud_hide or []:
            self.cmd(f"booth hud {hide}")

    def hold_at(self, frame: int) -> None:
        self.cmd(f"booth hold {frame}")
        self.wait_for(lambda s: self.booth(s).get("held") and self.booth(s).get("fightFrame", -1) >= frame,
                      90.0 + frame / 30.0, f"the hold at fight frame {frame}")

    def advance(self, line: str, frames: int, poll_s: float = 0.25) -> int:
        """`booth play`/`booth run` for `frames` fight frames, then the hold; returns the fight frame."""
        before = self.booth(self.status()).get("fightFrame", 0)
        self.cmd(line)
        status = self.wait_for(lambda s: self.booth(s).get("held") and self.booth(s).get("fightFrame", -1) >= before + frames,
                               60.0 + frames / 20.0, f"{line} ({frames} frames)", poll_s=poll_s)
        return self.booth(status).get("fightFrame", -1)

    def scale(self, p1: float, p2: float) -> None:
        # "a side's fighter is not known yet" is answered until the game has drawn both fighters once.
        deadline = time.monotonic() + 15.0
        while True:
            reply = self.cmd_raw(f"booth scale {p1} {p2}")
            if reply.startswith("ok"):
                break
            if "not known yet" not in reply and "did not answer" not in reply or time.monotonic() > deadline:
                raise CaptureError(f"booth scale: {reply[:200]}")
            time.sleep(0.5)
        self.wait_presents(SETTLE_PRESENTS)

    def hide(self, p1: bool, p2: bool) -> None:
        """Hides (or shows) each side's whole fighter actor (`booth hide`): meshes, cloth and
        shadow, where a scale to 0.001 leaves the cloth on the floor. A mod without the command
        falls back to the scale."""
        deadline = time.monotonic() + 15.0
        while True:
            reply = self.cmd_raw(f"booth hide {int(p1)} {int(p2)}")
            if reply.startswith("ok"):
                break
            if "no booth command" in reply:
                self.scale(0.001 if p1 else 1.0, 0.001 if p2 else 1.0)
                return
            if "not known yet" not in reply and "did not answer" not in reply or time.monotonic() > deadline:
                raise CaptureError(f"booth hide: {reply[:200]}")
            time.sleep(0.5)
        self.wait_presents(SETTLE_PRESENTS)

    def camera_check(self) -> list[str]:
        """The camera words (CameraUnit[0]+0x450) must be the same on two reads a few presents
        apart: a camera still moving is not a frozen frame. A copy whose peek is not answered
        (`error`) is warned about once and let through."""
        deadline = time.monotonic() + CAMERA_TIMEOUT_S
        previous = None
        while True:
            reply = self.cmd_raw(CAMERA_PEEK)
            if not reply.startswith("ok"):
                global _CAMERA_WARNED
                if not _CAMERA_WARNED:
                    _CAMERA_WARNED = True
                    self.log.say(f"copy {self.index}: camera check unavailable ({reply}); continuing without it")
                return []
            words = reply.split()[1:]
            if words == previous:
                return words
            previous = words
            if time.monotonic() > deadline:
                raise CaptureError(f"camera not stable: {' '.join(words)}")
            self.wait_presents(4)

    def camera_now(self) -> "bc.Camera":
        """The camera the held frame is drawn through (boothcamera.CAMERA_PEEK)."""
        reply = self.cmd_raw(bc.CAMERA_PEEK)
        camera = bc.Camera.from_reply(reply)
        if camera is None:
            raise CaptureError(f"the camera did not read: {reply[:120]}")
        return camera

    def cmd_raw(self, line: str) -> str:
        return lib.request(self.index, line)

    def grab(self, path: Path, depth: bool, after: int = 0, poll_s: float = 0.25) -> int:
        path.parent.mkdir(parents=True, exist_ok=True)
        posix = path.as_posix()
        quoted = f'"{posix}"' if " " in posix else posix  # the mod wants a path with spaces in double quotes
        # ui=off: the colour from before the frame's first interface draw (no HUD, no menu left over,
        # no overlay toast).
        self.cmd(f"booth grab {quoted} ui=off" + ("" if depth else " depth=off") + (f" after={after}" if after else ""))
        want = posix.lower()

        def taken(s: dict) -> bool:
            b = self.booth(s)
            last = b.get("lastGrab") or {}
            return not b.get("pending") and str(last.get("path", "")).replace("\\", "/").lower() == want

        status = self.wait_for(taken, GRAB_TIMEOUT_S, f"the grab {path.name}", poll_s=poll_s)
        last = self.booth(status)["lastGrab"]
        if not last.get("ok"):
            raise CaptureError(f"grab failed: {last.get('error') or 'no reason given'}")
        return self.booth(status).get("fightFrame", -1)

    # --- loads ---

    def look_pick(self, look: Look | None) -> str:
        if look is None:
            return "-"
        return f"{look[0]}:{look[1]}:{look[2]}"

    def wait_loaded(self) -> None:
        """Until SFV reports nothing loading for LOADED_FRAMES battle frames (booth.loading, the
        game's IsAsyncLoading): a fighter drawn before its costume loaded is grey."""
        self.wait_for(lambda s: not self.booth(s).get("loading", False) and self.booth(s).get("loadedFrames", LOADED_FRAMES) >= LOADED_FRAMES,
                      LOADED_TIMEOUT_GRAB_S, "the game to finish loading")

    def capture_looks(self, load: dict) -> None:
        """A look load, its grabs checked (boothpack.check_pair); a load whose grabs fail the checks
        is loaded again once, then its looks fail with the reason."""
        for attempt in range(1, LOOK_ATTEMPTS + 1):
            try:
                return self.capture_looks_once(load)
            except GrabCheckError as e:
                if attempt == LOOK_ATTEMPTS:
                    raise CaptureError(str(e)) from None
                self.log.say(f"copy {self.index}: {e}; loading it again")
                self.cmd("booth leave")
                self.to_menu()

    def capture_looks_once(self, load: dict) -> None:
        p1, p2 = load["looks"]
        key = "__".join(f"{c}-{n}-{m}" for c, n, m in (look for look in (p1, p2) if look))
        raw = self.store / "raw" / key
        stage = self.args.stage
        self.start_battle(self.look_pick(p1), self.look_pick(p2))
        if self.args.walk:
            self.hold_at(0)
            self.advance(f"booth play 0 RIGHT:{self.args.walk_frames}", self.args.walk_frames)
            self.advance(f"booth play 1 LEFT:{self.args.walk_frames}", self.args.walk_frames)
            now = self.booth(self.status()).get("fightFrame", 0)
            if now < self.args.frame:
                self.hold_at(self.args.frame)
        else:
            self.hold_at(self.args.frame)
        self.wait_loaded()
        self.wait_presents(SETTLE_PRESENTS)
        camera = self.camera_check()
        sides = [(0, p1), (1, p2)] if p2 else [(0, p1)]
        if len(sides) == 1:
            self.hide(False, True)  # the opponent out of the picture, so the mask is one fighter
        a_path, a2_path, b_path = raw / "a.bmp", raw / "a2.bmp", raw / "b.bmp"
        frame = self.grab(a_path, True)
        # The held frame grabbed again a little later must be the same picture (textures in, no
        # screen fading over it); else the later one becomes A and is checked again.
        for attempt in range(STABLE_TRIES):
            self.wait_presents(STABLE_PRESENTS)
            self.grab(a2_path, False)
            changed = bp.grabs_differ(a_path, a2_path)
            if changed <= bp.STABLE_MAX:
                break
            if attempt + 1 == STABLE_TRIES:
                raise GrabCheckError(f"the held frame kept changing ({changed:.1%} of the frame between two grabs)")
            frame = self.grab(a_path, True)
        depth_state = self.booth(self.status()).get("lastGrab", {}).get("depth", "")
        self.hide(True, True)  # the plate: the same frozen frame without the fighters
        self.grab(b_path, True)
        reference = self.store / "reference" / f"plate-{stage}-{self.args.frame}.png"
        checks, refused = bp.check_pair(a_path, b_path, reference)
        if refused:
            raise GrabCheckError(refused)
        checks["stable"] = round(changed, 5)
        meta = {"frame": frame, "stage": stage, "load": key, "camera": " ".join(camera), "depth": depth_state, "checks": checks}
        future = self.post.submit(self.make_masters, a_path, b_path, [p1, p2], meta, raw)
        self.pending.append(future)
        self.grabbed = True
        self.cmd("booth leave")
        self.to_menu()

    def make_masters(self, a_path: Path, b_path: Path, looks: list, meta: dict, raw: Path) -> None:
        stage = self.args.stage
        ok = True
        try:
            results = bp.make_masters(a_path, b_path, looks, self.store, self.args.mask, meta)
        except bp.BoothError as e:
            results = {0: e, 1: e}
        except Exception as e:  # noqa: BLE001 - a broken grab must not stop the run
            results = {0: bp.BoothError(f"mask error: {e!r}"), 1: bp.BoothError(f"mask error: {e!r}")}
        for i, look in enumerate(looks):
            if look is None:
                continue
            result = results.get(i)
            if isinstance(result, Exception):
                ok = False
                self.failed += 1
                self.log.failure(self.index, "looks", {"fighter": look[0], "costume": look[1], "colour": look[2]},
                                 f"mask: {result}", stage)
        if ok and not self.args.keep_raw:
            for f in raw.glob("*"):
                try:
                    f.unlink()
                except OSError:
                    pass
            try:
                raw.rmdir()
            except OSError:
                pass

    def capture_loadout(self, load: dict) -> None:
        """The fighter's V-Skill and V-Trigger pictures. A battle per side (the other side's actor
        hidden for good: showing a hidden side again would show a partner the game keeps hidden,
        R. Mika's). For each move: the plate (the fighter scaled away), the fighter standing (pre),
        the press, then a burst of grabs every `auto.step` fight frames up to `auto.span`; the
        picture is chosen from the burst afterwards (save_loadout, tools/boothburst.py)."""
        code = load["fighter"]
        fighter = self.space["fighters"][code]
        costume = next(iter(fighter["costumes"]))
        colour = fighter["costumes"][costume][0]
        todo = loadout_todo(self.store, self.space, code) if not self.args.force else \
            [(s, p, s + 1) for s in (0, 1) for p in ("vs", "vt") if s + 1 <= fighter[p]]
        if not todo:
            return
        cfg = self.cfg
        auto = cfg["auto"]
        picks = []
        for side in (0, 1):
            vs = min(side + 1, max(fighter["vs"], 1))
            vt = min(side + 1, max(fighter["vt"], 1))
            picks.append(f"{code}:{costume}:{colour}:{vs}:{vt}")
        raw_root = self.store / "raw" / f"loadout-{code}"
        for n_side, side in enumerate(sorted({t[0] for t in todo})):
            if n_side:
                self.cmd("booth leave")
                self.to_menu()
            self.start_battle(picks[0], picks[1])
            self.hold_at(cfg["start"])
            self.wait_loaded()
            self.wait_presents(SETTLE_PRESENTS)
            self.hide(side != 0, side != 1)  # the opponent's whole actor away
            steps = [t for t in todo if t[0] == side]
            steps.sort(key=lambda t: t[1] != "vs")  # V-Skill first, then V-Trigger
            for n, (_, prefix, choice) in enumerate(steps):
                stem = f"{prefix}{choice}"
                # The stage behind the fighter (scaled away; never hidden, see above), then the
                # fighter standing again once its cloth has settled.
                # The plate keeps its depth, and every grab the camera it was drawn through: the
                # plate is seen again through each burst grab's camera (boothburst, boothcamera).
                self.scale(0.001 if side == 0 else 1.0, 0.001 if side == 1 else 1.0)
                plate = raw_root / f"{stem}-plate.bmp"
                self.grab(plate, True)
                plate_camera = self.camera_now()
                self.scale(1.0, 1.0)
                self.advance(f"booth run {auto['settle_frames']}", auto["settle_frames"])
                pre = raw_root / f"{stem}-pre.bmp"
                self.grab(pre, False)
                pre_camera = self.camera_now()
                buttons = "MP+MK" if prefix == "vs" else "HP+HK"
                self.advance(f"booth play {side} {buttons}:{cfg['press']}", cfg["press"])
                override = override_after_press(cfg, code, prefix, choice)
                after = sorted(set(range(auto["step"], auto["span"] + 1, auto["step"])) | ({override} if override else set()))
                burst, now = [], 0
                for t in after:
                    self.advance(f"booth run {t - now}", t - now, poll_s=BURST_POLL_S)
                    now = t
                    path = raw_root / f"{stem}-{t:03d}.bmp"
                    frame = self.grab(path, False, after=BURST_AFTER_PRESENTS, poll_s=BURST_POLL_S)
                    burst.append((t, path, frame, self.camera_now()))
                self.post.submit(self.save_loadout, code, prefix, choice, side, (pre, pre_camera), (plate, plate_camera),
                                 burst, override)
                if n + 1 < len(steps):
                    self.advance(f"booth run {cfg['gap']}", cfg["gap"])
        self.cmd("booth leave")
        self.to_menu()

    def save_loadout(self, code: str, prefix: str, choice: int, side: int, pre: tuple, plate: tuple,
                     burst: list, override: int | None) -> None:
        """Chooses the burst's grab (an override's frame where loadout-frames.json gives one, else
        boothburst's choice, else the grab nearest the default frame), and saves its 16:9 crop
        lossless with what was chosen and why; --burst also writes every grab, scored, to
        <store>\\burst\\<CODE> for review."""
        stage = self.args.stage
        part = bp.PART_NAMES[prefix]
        try:
            def read(path: Path) -> np.ndarray:
                with Image.open(path) as im:
                    return np.asarray(im.convert("RGB"))

            (pre_path, pre_camera), (plate_path, plate_camera) = pre, plate
            depth = bp.read_depth(plate_path)
            if depth is None:
                raise bp.BoothError("the plate has no depth")
            grabs = [read(path) for _, path, _, _ in burst]
            scores = bb.score_move(read(plate_path), depth, plate_camera, read(pre_path), pre_camera, grabs,
                                   [c for _, _, _, c in burst])
            times = [t for t, _, _, _ in burst]
            if override:
                index, method = times.index(override), "override"
            else:
                index = bb.choose(scores, times)
                method = "auto"
                if index is None:
                    wanted = default_after_press(self.cfg, prefix)
                    index, method = min(range(len(times)), key=lambda i: abs(times[i] - wanted)), "default"
            t, path, frame, _ = burst[index]
            grab = grabs[index]
            box = bb.subject_box(scores[index])
            x0, y0, crop_w, crop_h = bb.frame_window(box, grab.shape[0], grab.shape[1], side=side)
            picture = Image.fromarray(grab).crop((x0, y0, x0 + crop_w, y0 + crop_h))
            target = bp.loadout_master_path(self.store, code, prefix, choice)
            target.parent.mkdir(parents=True, exist_ok=True)
            picture.save(target)
            meta = {"frame": frame, "afterPress": t, "method": method, "stage": stage, "crop": [x0, y0, crop_w, crop_h],
                    "scores": [[tt, round(g.score, 4), g.usable, round(g.stage, 4), g.flash] for tt, g in zip(times, scores)]}
            target.with_suffix(".json").write_text(json.dumps(meta), encoding="utf-8")
            if method != "auto":
                self.log.say(f"copy {self.index}: {code} {prefix}{choice}: {method} frame {t} after the press")
            if self.args.burst:
                out = self.store / "burst" / code
                out.mkdir(parents=True, exist_ok=True)
                for old in out.glob(f"{prefix}{choice}-t*.jpg"):
                    old.unlink(missing_ok=True)
                for i, ((tt, p, _, _), g) in enumerate(zip(burst, scores)):
                    thumb = Image.fromarray(grabs[i]).resize((480, 270), Image.Resampling.BILINEAR)
                    mark = "-chosen" if i == index else ("" if g.usable else "-unused")
                    thumb.save(out / f"{prefix}{choice}-t{tt:03d}-s{round(g.score * 1000):03d}{mark}.jpg", quality=80)
            if not self.args.keep_raw:
                for _, p, _, _ in burst:
                    p.unlink(missing_ok=True)
                for p in (pre_path, plate_path):
                    for f in p.parent.glob(p.name + "*"):
                        f.unlink(missing_ok=True)
        except Exception as e:  # noqa: BLE001
            self.failed += 1
            self.log.failure(self.index, "loadout", {"fighter": code, "part": part, "choice": choice},
                             f"loadout picture: {e!r}", stage)

    # --- the copy's run ---

    def label(self, load: dict) -> list[dict]:
        if load["kind"] == "loadout":
            fighter = self.space["fighters"][load["fighter"]]
            return [{"fighter": load["fighter"], "part": bp.PART_NAMES[p], "choice": c}
                    for p in ("vs", "vt") for c in (1, 2) if c <= fighter[p]]
        return [{"fighter": c, "costume": n, "colour": m} for c, n, m in (look for look in load["looks"] if look)]

    def run(self) -> None:
        try:
            time.sleep(self.index * self.args.stagger)
            self.boot()
            self.started = time.monotonic()
            for load in self.loads:
                if self.stop.is_set():
                    break
                if self.since_boot >= self.args.relaunch_every:
                    self.log.say(f"copy {self.index}: relaunching after {self.since_boot} loads")
                    self.boot()
                phase = "loadout" if load["kind"] == "loadout" else "looks"
                self.attempted += 1
                self.grabbed = False
                try:
                    (self.capture_loadout if load["kind"] == "loadout" else self.capture_looks)(load)
                    self.done += 1
                except Stopped:
                    break
                except (CaptureError, lib.SfveError, ValueError, KeyError, OSError) as e:
                    for look in self.label(load):
                        if "colour" in look and self.grabbed:
                            continue  # grabbed and handed to masking; only the leave failed: not a failed look
                        # (A master already there is no proof: with --force it is the old one.)
                        self.failed += 1
                        self.log.failure(self.index, phase, look, str(e), self.args.stage)
                    try:
                        self.recover()
                    except CaptureError as boot_error:
                        self.log.say(f"copy {self.index} stops: {boot_error}")
                        return
                self.since_boot += 1
            for future in self.pending:
                future.result()
        except Stopped:
            pass
        except CaptureError as e:
            self.log.say(f"copy {self.index} stops: {e}")
        finally:
            try:
                kill_copy(self.index)
            except Exception:  # noqa: BLE001
                pass

    def progress(self) -> str:
        minutes = max((time.monotonic() - self.started) / 60.0, 1e-9) if self.started else 0
        rate = self.done / minutes if minutes else 0.0
        rate = self.attempted / minutes if minutes else 0.0
        left = len(self.loads) - self.attempted
        eta = f"{left / rate / 60:.1f} h" if rate > 0 else "?"
        return (f"copy {self.index}: {self.attempted}/{len(self.loads)} loads ({self.failed} looks failed), "
                f"{rate:.1f} loads/min, ETA {eta}")


# --- command line ----------------------------------------------------------------------------------------

def plan_text(args, loads: list[dict], space: dict) -> str:
    looks = sum(2 if (l["kind"] == "looks" and l["looks"][1]) else 1 for l in loads if l["kind"] == "looks")
    pictures = sum(len(loadout_todo(Path(args.store), space, l["fighter"])) for l in loads if l["kind"] == "loadout")
    n = len(loads)
    per_copy = -(-n // args.copies) if n else 0
    hours = per_copy * args.secs_per_load / 3600.0
    return (f"{n} loads ({looks} looks, {pictures} loadout pictures) over {args.copies} copies: {per_copy} loads each; "
            f"at {args.secs_per_load:.0f} s per load that is {hours:.1f} h. "
            f"Finishing in 3 h needs {n / 3 / 60:.1f} loads/min over all copies.")


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--copies", type=int, default=4, help="game copies at once, each with its own pipe and slice (default 4)")
    p.add_argument("--phase", choices=["1", "2", "3", "all"], default="all",
                   help="1: each costume's first colour, 2: all other colours, 3: loadout pictures")
    p.add_argument("--store", default=bp.DEFAULT_STORE)
    p.add_argument("--pick-space", default=str(bp.DEFAULT_PICK_SPACE))
    p.add_argument("--stage", default="TRN")
    p.add_argument("--frame", type=int, default=60, help="fight frame the battle is held at for the grabs (default 60)")
    p.add_argument("--sides", type=int, choices=[1, 2], default=2, help="2: a look per side (default); 1: P1 only, P2 scaled away")
    p.add_argument("--mask", choices=["depth", "colour"], default="depth")
    p.add_argument("--walk", action="store_true", help="walk the fighters in (P1 right, P2 left) before the hold")
    p.add_argument("--walk-frames", type=int, default=40)
    p.add_argument("--target", choices=["versus", "training"], default="versus")
    p.add_argument("--limit", type=int, default=0, help="at most N looks (loads for phase 3), for pilots")
    p.add_argument("--looks", help="CODE:costume:colour,... exactly these looks (a pilot)")
    p.add_argument("--looks-file", help="exactly the looks in this file, one CODE:costume:colour a line "
                                        "(boothpack.py check writes one); use with --force to capture masters again")
    p.add_argument("--fighters", help="CODE,CODE only these fighters")
    p.add_argument("--per-fighter", type=int, default=0, help="at most N looks per fighter")
    p.add_argument("--retry-failed", action="store_true", help="the looks the capture log lists as failed and that still have no master")
    p.add_argument("--force", action="store_true", help="capture again even where a master exists")
    p.add_argument("--burst", action="store_true", help="phase 3: also write every burst grab, scored, to <store>\\burst for review")
    p.add_argument("--keep-raw", action="store_true", help="keep the raw grabs (about 30 MB a load) instead of deleting them once the masters exist")
    p.add_argument("--relaunch-every", type=int, default=200, help="restart a copy's game after this many loads")
    p.add_argument("--stagger", type=float, default=6.0, help="seconds between copy starts")
    p.add_argument("--load-stuck", type=float, default=25.0,
                   help="seconds a launch may take to load before the copy is relaunched and the load tried again (a load takes 2-7 s)")
    p.add_argument("--menu-settle", type=float, default=4.0,
                   help="seconds on the main menu after a leave before the next launch (default 4: less hangs the next load of the same stage)")
    p.add_argument("--show-hud", action="store_true", help="leave the HUD on screen (by default `booth hudhide on` hides it for every grab)")
    p.add_argument("--hud-hide", action="append", help='a `booth hud` argument ("<method> <number> [text]") sent after each load, to hide the HUD')
    p.add_argument("--game-option", action="append", help="extra game option for every copy")
    p.add_argument("--plan", action="store_true", help="print the worklist size and the time estimate; start nothing")
    p.add_argument("--secs-per-load", type=float, default=30.0, help="for --plan: seconds a load takes one copy (measure it with a pilot)")
    p.add_argument("--progress", type=float, default=30.0, help="seconds between progress lines")
    args = p.parse_args(argv)
    if args.copies < 1:
        p.error("--copies must be at least 1")
    store = Path(args.store)
    space = bp.load_space(args.pick_space)
    if args.stage not in space["stages"]:
        p.error(f"stage {args.stage} is not in the pick space")
    cfg = load_loadout_config()
    loads = build_loads(args, space, store)
    print(plan_text(args, loads, space))
    if args.plan or not loads:
        return 0
    os.environ.setdefault("SFVE_RES", "1920x1080")  # the grabs are 1920x1080 whatever the window says
    lib.begin_run()
    take_launch_lock()
    log = Log(store / "capture-log.jsonl")
    stop = threading.Event()
    post = ThreadPoolExecutor(max_workers=max(2, args.copies))
    copies = [Copy(i, args, space, store, log, slice_for(loads, i, args.copies), stop, post, cfg) for i in range(args.copies)]
    threads = [threading.Thread(target=c.run, name=f"copy{c.index}", daemon=True) for c in copies if c.loads]
    started = time.monotonic()
    try:
        for t in threads:
            t.start()
        last = 0.0
        while any(t.is_alive() for t in threads):
            time.sleep(1)
            if time.monotonic() - last >= args.progress:
                last = time.monotonic()
                for c in copies:
                    if c.loads:
                        log.say(c.progress())
    except KeyboardInterrupt:
        log.say("stopping: waiting for the copies to finish their step")
        stop.set()
        for t in threads:
            t.join(timeout=60)
    finally:
        stop.set()
        post.shutdown(wait=True)
        lib.kill_all()
        release_launch_lock()
    done = sum(c.done for c in copies)
    failed = sum(c.failed for c in copies)
    log.say(f"finished in {(time.monotonic() - started) / 60:.1f} min: {done} loads captured, {failed} failures "
            f"(log: {store / 'capture-log.jsonl'}; --retry-failed tries those again)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
