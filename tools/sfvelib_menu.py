"""sfvelib: menu routes: pad steps from the main menu into a battle."""

from __future__ import annotations

import json
import time
from pathlib import Path

from sfvelib_config import SfveError
from sfvelib_pipe import request, request_ok, status_pid
from sfvelib_windows import focus, screenshot


# Main menu (cursor on ARCADE, as after boot) to a Player 1 vs Player 2 battle
# on the preselected stage with each side's preselected fighter. Each step is
# (pad slot, steps, seconds to wait after queueing). Needs pads 0 and 1 virtual.
VERSUS_FROM_MAIN_MENU = [
    (0, "DOWN:120 -:500 DOWN:120 -:500 A:150 -:300", 3.5),     # VERSUS
    (0, "A:150 -:300", 3.0),                                    # PLAYER 1 VS PLAYER 2
    (0, " ".join(["DOWN:120 -:400"] * 4) + " A:150 -:300", 0.5),  # P1: Proceed to Match
    (1, "DOWN:120 -:500 A:150 -:300", 6.0),                      # P2: Proceed to Match
    (0, "A:150 -:800 A:150 -:300", 8.0),                         # stage, then its setting
    (0, " ".join(["A:120 -:700"] * 9), 0.0),                     # P1 fighter, costume, color, V-Skill...
    (1, " ".join(["A:120 -:700"] * 9), 25.0),                    # P2 likewise; VS screen and load
]


_MASH_DIRECTIONS = ["-", "UP", "DOWN", "LEFT", "RIGHT", "DOWN+LEFT", "DOWN+RIGHT", "UP+LEFT", "UP+RIGHT"]
_MASH_BUTTONS = ["", "X", "Y", "RB", "A", "B", "RT", "X+A", "Y+B"]


def mash_steps(seconds: float, seed: int) -> str:
    """Seeded random pad steps (directions and attacks, never Start) lasting about `seconds`."""
    import random
    rng = random.Random(seed)
    steps, total = [], 0
    while total < seconds * 1000:
        held = rng.choice(_MASH_DIRECTIONS)
        button = rng.choice(_MASH_BUTTONS)
        if button:
            held = button if held == "-" else f"{held}+{button}"
        ms = rng.randint(30, 220)
        steps.append(f"{held}:{ms}")
        total += ms
    return " ".join(steps)


def run_route(index: int, route: list[tuple[int, str, float]]) -> None:
    """Plays a menu route on a running copy's virtual pads (menus need its window in front)."""
    focus(status_pid(index))
    for slot in sorted({slot for slot, _, _ in route}):
        request_ok(index, f"virtual {slot} on")
    time.sleep(1.0)
    for slot, steps, wait_s in route:
        reply = request(index, f"pad {slot} {steps}")
        if not reply.startswith("ok"):
            raise SfveError(f"instance {index}: {reply}")
        time.sleep(wait_s)


MAIN_MENU_SCENE = "0x2D98660"  # the main menu state's vtable rva (status "scene")


def scene(index: int) -> str:
    """The game's current scene state kind, or "" while unknown."""
    try:
        return json.loads(request(index, "status", idempotent=True)[3:]).get("scene", "")
    except (OSError, ValueError, SfveError):
        return ""


def boot_to_menu(index: int, timeout: float = 90.0, shot_on_timeout: Path | None = None, bring_to_front: bool = True) -> None:
    """From a fresh launch to the main menu. The mod hides the offline start-up
    dialogs and leaves the title by itself, so this only waits. On a timeout
    the copy's window is saved to `shot_on_timeout`, when given. A run whose
    copies play in the background (-sfve_background=1) may leave the window
    where it is (bring_to_front=False): Windows refuses it on a locked desktop."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if scene(index) == MAIN_MENU_SCENE:
            time.sleep(1)  # the menu's own intro
            if bring_to_front:
                focus(status_pid(index))
            return
        time.sleep(1)
    if shot_on_timeout:
        try:
            shot_on_timeout.parent.mkdir(parents=True, exist_ok=True)
            screenshot(status_pid(index), shot_on_timeout)
        except (SfveError, OSError):
            pass
    raise TimeoutError(f"instance {index} did not reach the main menu in {timeout:.0f} s")


def fight(index: int) -> None:
    """From a fresh launch into a Player 1 vs Player 2 battle (fight on screen)."""
    boot_to_menu(index)
    run_route(index, VERSUS_FROM_MAIN_MENU)
    time.sleep(8)
