"""Test automation for SFV Ember: deploy, launch, control, capture.

Shared by the command line (sfve.py) and the MCP server (sfve_mcp.py).
Windows only; standard library only.

Two ways to start a copy:
* through SFV Ember's Launcher.exe (SFVE_VIA=launcher), as players do: the
  launcher starts the game with the mod injected from its own folder
  (SFVE_PACKAGE_DIR: a staged package, or the build tree), and nothing needs
  to be in the game folder. Copy N > 0 is the launcher's test instance
  (--instance N); the test's game options go through --game-option;
* straight to the exe, with the xinput1_3.dll proxy deployed beside it
  (SFVE_VIA=proxy, the default: deploy first).

Safety rules this module enforces:
* A copy is only launched with the mod in place (the proxy and the mod
  deployed, or the launcher, the mod and the helper in the package folder),
  and is killed if its control pipe (which the mod opens only after the
  network guard is installed) does not answer within GUARD_TIMEOUT_S.
* Nothing here uses the game's online modes.
* Only copies this module started are killed: their launchers' and games'
  process ids are kept in LAUNCHED (kill_all).
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import os
import shutil
import struct
import subprocess
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

GAME_DIR = Path(os.environ.get(
    "SFVE_GAME_DIR",
    r"G:\SteamLibrary\steamapps\common\StreetFighterV\StreetFighterV\Binaries\Win64"))
BUILD_DIR = Path(os.environ.get("SFVE_BUILD_DIR", r"D:\sfv-ember-build"))
GAME_EXE = GAME_DIR / "StreetFighterV.exe"
MOD_FILES = ("xinput1_3.dll", "sfv_ember.dll", "sf4-net.exe")  # sf4-net: scripts/build-helper.ps1
# Ember's own pictures the shell draws (the menu backdrop, button prompts and
# flags), which the build copies from the Ember library into its assets/.
ASSETS = "assets"
GUARD_TIMEOUT_S = 20.0
STEAM_APP_ID = "310950"
VIA = os.environ.get("SFVE_VIA", "proxy")
PACKAGE_DIR = Path(os.environ.get("SFVE_PACKAGE_DIR", str(BUILD_DIR)))
LAUNCHER_FILES = ("Launcher.exe", "sfv_ember.dll", "sf4-net.exe")
# The mod's logs (mod/Log.cxx) and the launcher's, however the game started.
LOG_DIR = Path(os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))) / "SFVEmber" / "logs"
# The copies this module started, by index: {"launcher": pid or 0, "game": pid}.
LAUNCHED = Path(os.environ.get("TEMP", str(BUILD_DIR))) / "sfve-launched.json"


class SfveError(RuntimeError):
    pass


# --- deploy -----------------------------------------------------------------

def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _deployed_files() -> list[str]:
    """The proxy, the mod, its helper and its assets, relative to the build."""
    assets = BUILD_DIR / ASSETS
    pictures = [str(p.relative_to(BUILD_DIR)) for p in sorted(assets.rglob("*")) if p.is_file()] if assets.exists() else []
    return list(MOD_FILES) + pictures


def deploy() -> list[str]:
    """Copies the built proxy, mod and assets next to the game exe."""
    copied = []
    for name in _deployed_files():
        src, dst = BUILD_DIR / name, GAME_DIR / name
        if not src.exists():
            raise SfveError(f"{src} is not built")
        if not dst.exists() or _sha256(src) != _sha256(dst):
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            copied.append(name)
    return copied


def undeploy() -> list[str]:
    """Removes the proxy, mod and assets from the game folder (logs stay)."""
    removed = []
    for name in MOD_FILES:
        path = GAME_DIR / name
        if path.exists():
            path.unlink()
            removed.append(name)
    if (GAME_DIR / ASSETS).exists():
        shutil.rmtree(GAME_DIR / ASSETS)
        removed.append(ASSETS)
    return removed


def deployed_matches_build() -> bool:
    return all((GAME_DIR / n).exists() and _sha256(GAME_DIR / n) == _sha256(BUILD_DIR / n) for n in _deployed_files())


# --- control pipe -------------------------------------------------------------

def pipe_path(instance: int) -> str:
    return rf"\\.\pipe\sfve-{instance}"


def request(instance: int, line: str, timeout_s: float = 5.0) -> str:
    """Sends one command line to a running copy; returns its reply line."""
    deadline = time.monotonic() + timeout_s
    while True:
        try:
            with open(pipe_path(instance), "r+b", buffering=0) as pipe:
                pipe.write(line.encode() + b"\n")
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
            if time.monotonic() > deadline:
                raise SfveError(f"instance {instance} is not answering on {pipe_path(instance)}")
            time.sleep(0.1)


def status_pid(index: int) -> int:
    reply = request(index, "status")
    if not reply.startswith("ok "):
        raise SfveError(f"instance {index}: {reply}")
    return int(reply.split('"pid":', 1)[1].split(",", 1)[0])


# --- processes ----------------------------------------------------------------

@dataclass
class Instance:
    index: int
    pid: int


def running_pids() -> list[int]:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq StreetFighterV.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    return [int(line.split('","')[1]) for line in out.splitlines() if line.startswith('"StreetFighterV.exe"')]


def _launched() -> dict[str, dict[str, int]]:
    try:
        return json.loads(LAUNCHED.read_text())
    except (OSError, ValueError):
        return {}


def _remember(index: int, launcher: int, game: int) -> None:
    started = _launched()
    started[str(index)] = {"launcher": launcher, "game": game}
    LAUNCHED.write_text(json.dumps(started))


def _game_options(index: int, mode: str, extra: list[str], windowed: bool) -> list[str]:
    options = [f"-sfve_instance={index}", f"-sfve_mode={mode}"]
    if windowed:
        # SFVE_RES ("1920x1080") sets the window's size; 960x540 by default.
        width, height = os.environ.get("SFVE_RES", "960x540").lower().split("x")
        options += ["-windowed", f"-ResX={int(width)}", f"-ResY={int(height)}"]
    return options + extra


def launch(index: int, mode: str = "probe", extra: list[str] | None = None, windowed: bool = True) -> Instance:
    """Starts one copy and waits until its guard is confirmed (or kills it)."""
    options = _game_options(index, mode, extra or [], windowed)
    log_path(index).unlink(missing_ok=True)  # each run starts its own log
    if VIA == "launcher":
        missing = [name for name in LAUNCHER_FILES if not (PACKAGE_DIR / name).exists()]
        if missing:
            raise SfveError(f"{PACKAGE_DIR} has no {', '.join(missing)}: build or stage SFV Ember first")
        # The test's options come first on the game's command line, so they
        # take the place of the launcher's own (ModConfig reads the first).
        args = [str(PACKAGE_DIR / "Launcher.exe")]
        if index:
            args += ["--instance", str(index)]
        args += [f"--game-option={option}" for option in options if not option.startswith("-sfve_instance=")]
        proc = subprocess.Popen(args, cwd=PACKAGE_DIR, creationflags=subprocess.DETACHED_PROCESS)
        launcher_pid = proc.pid
    else:
        if not deployed_matches_build():
            raise SfveError("the game folder does not hold the current build: run deploy first")
        # Steam sets these when it starts a game; its DRM wrapper exits (0x35) without them.
        env = dict(os.environ, SteamAppId=STEAM_APP_ID, SteamGameId=STEAM_APP_ID)
        proc = subprocess.Popen([str(GAME_EXE)] + options, cwd=GAME_DIR, env=env, creationflags=subprocess.DETACHED_PROCESS)
        launcher_pid = 0
    _remember(index, launcher_pid, 0 if launcher_pid else proc.pid)

    def refuse(why: str) -> SfveError:
        _kill_copy(launcher_pid, 0 if launcher_pid else proc.pid)
        return SfveError(f"instance {index} {why}; killed")

    try:
        reply = request(index, "status", timeout_s=GUARD_TIMEOUT_S)
    except SfveError:
        raise refuse(f"did not confirm its network guard within {GUARD_TIMEOUT_S:.0f} s")
    if not reply.startswith("ok "):
        raise refuse(f"answered '{reply}'")
    game_pid = int(reply.split('"pid":', 1)[1].split(",", 1)[0])
    _remember(index, launcher_pid, game_pid)
    # The pipe answers as soon as the guard is in; the mode's own commands
    # (freeze, host, ...) exist once the battle status does.
    if mode != "inert":
        deadline = time.monotonic() + GUARD_TIMEOUT_S
        while '"battle":' not in reply and time.monotonic() < deadline:
            time.sleep(0.2)
            reply = request(index, "status")
        if '"battle":' not in reply:
            _kill_copy(launcher_pid, game_pid)
            raise SfveError(f"instance {index} did not finish starting the mod; killed")
    return Instance(index, game_pid)


def kill_pid(pid: int) -> None:
    subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)


def _kill_copy(launcher: int, game: int) -> None:
    # The launcher's tree holds its game; a game started directly is alone.
    if launcher:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(launcher)], capture_output=True)
    if game:
        kill_pid(game)


def kill_all(timeout_s: float = 15.0) -> list[int]:
    """Kills the copies this module started (and their launchers), and waits
    until they are gone, their pipes with them. A copy anything else started
    is left running."""
    started = _launched()
    ours = {pid for copy in started.values() for pid in (copy.get("launcher", 0), copy.get("game", 0)) if pid}
    games = {copy.get("game", 0) for copy in started.values()} - {0}
    for copy in started.values():
        _kill_copy(copy.get("launcher", 0), copy.get("game", 0))
    deadline = time.monotonic() + timeout_s
    while set(running_pids()) & games and time.monotonic() < deadline:
        time.sleep(0.2)
    LAUNCHED.unlink(missing_ok=True)
    return sorted(ours)


# --- menu routes ----------------------------------------------------------------

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
        request(index, f"virtual {slot} on")
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
        return json.loads(request(index, "status")[3:]).get("scene", "")
    except (OSError, ValueError, SfveError):
        return ""


def boot_to_menu(index: int, timeout: float = 90.0, shot_on_timeout: Path | None = None) -> None:
    """From a fresh launch to the main menu. The mod hides the offline start-up
    dialogs and leaves the title by itself, so this only waits. On a timeout
    the copy's window is saved to `shot_on_timeout`, when given."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if scene(index) == MAIN_MENU_SCENE:
            time.sleep(1)  # the menu's own intro
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


# --- logs ---------------------------------------------------------------------

def log_path(index: int) -> Path:
    name = "sfv_ember.log" if index == 0 else f"sfv_ember.{index}.log"
    return LOG_DIR / name


def tail(index: int, lines: int = 40) -> list[str]:
    path = log_path(index)
    if not path.exists():
        return []
    return path.read_text(errors="replace").splitlines()[-lines:]


def count_log(index: int, text: str) -> int:
    """How many of a copy's log lines hold `text`."""
    path = log_path(index)
    return sum(text in line for line in path.read_text(errors="replace").splitlines()) if path.exists() else 0


# --- window capture -------------------------------------------------------------

_user32 = ctypes.WinDLL("user32", use_last_error=True)
_gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
_EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
_user32.GetWindowDC.restype = wt.HDC
_user32.GetWindowDC.argtypes = [wt.HWND]
_user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
_user32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
_user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
_gdi32.CreateCompatibleDC.restype = wt.HDC
_gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
_gdi32.CreateCompatibleBitmap.restype = wt.HBITMAP
_gdi32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
_gdi32.SelectObject.restype = wt.HGDIOBJ
_gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
_gdi32.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
_gdi32.DeleteObject.argtypes = [wt.HGDIOBJ]
_gdi32.DeleteDC.argtypes = [wt.HDC]


def main_window(pid: int) -> int:
    """The largest visible top-level window of a process."""
    best, best_area = 0, 0

    def visit(hwnd, _):
        nonlocal best, best_area
        owner = wt.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and _user32.IsWindowVisible(hwnd):
            r = wt.RECT()
            _user32.GetWindowRect(hwnd, ctypes.byref(r))
            area = (r.right - r.left) * (r.bottom - r.top)
            if area > best_area:
                best, best_area = hwnd, area
        return True

    _user32.EnumWindows(_EnumWindowsProc(visit), 0)
    if not best:
        raise SfveError(f"process {pid} has no visible window")
    return best


def focus(pid: int) -> None:
    """Brings a copy's window to the front (SFV menus only take input there).

    Windows lets a process take the foreground right after keyboard input, so
    a bare Alt press and release comes first.
    """
    hwnd = main_window(pid)
    VK_MENU, KEYEVENTF_KEYUP, SW_RESTORE = 0x12, 0x0002, 9
    _user32.keybd_event(VK_MENU, 0, 0, 0)
    _user32.keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0)
    _user32.ShowWindow(hwnd, SW_RESTORE)
    if not _user32.SetForegroundWindow(hwnd):
        raise SfveError(f"could not bring process {pid}'s window to the front")


def close_window(pid: int) -> None:
    """Asks a copy's window to close as its title bar's close button does
    (WM_CLOSE, which Process.CloseMainWindow sends)."""
    WM_CLOSE = 0x0010
    if not _user32.PostMessageW(main_window(pid), WM_CLOSE, 0, 0):
        raise SfveError(f"could not post WM_CLOSE to process {pid}'s window")


def _png(width: int, height: int, rgb_rows: list[bytes]) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\x00" + row for row in rgb_rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def _dpi_aware() -> None:
    """Window sizes in the game's own pixels: a process the system scales
    reads a scaled client size (768x432 for a 960x540 window at 125%) while
    PrintWindow draws the window's real pixels, so a capture kept only its
    top-left part."""
    try:
        _user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2
    except (AttributeError, OSError):
        pass  # before Windows 10 1703: the scaled size, as before


def screenshot(pid: int, out: Path) -> Path:
    """Captures a game window (even when covered) to a PNG."""
    _dpi_aware()
    hwnd = main_window(pid)
    r = wt.RECT()
    _user32.GetClientRect(hwnd, ctypes.byref(r))
    width, height = r.right, r.bottom
    window_dc = _user32.GetWindowDC(hwnd)
    mem_dc = _gdi32.CreateCompatibleDC(window_dc)
    bitmap = _gdi32.CreateCompatibleBitmap(window_dc, width, height)
    old = _gdi32.SelectObject(mem_dc, bitmap)
    try:
        PW_CLIENTONLY, PW_RENDERFULLCONTENT = 1, 2
        if not _user32.PrintWindow(hwnd, mem_dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT):
            raise SfveError("PrintWindow failed")
        header = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 24, 0, 0, 0, 0, 0, 0)
        stride = (width * 3 + 3) & ~3
        buffer = ctypes.create_string_buffer(stride * height)
        _gdi32.GetDIBits(mem_dc, bitmap, 0, height, buffer, header, 0)
        rows = []
        for y in range(height):
            bgr = buffer.raw[y * stride: y * stride + width * 3]
            rgb = bytearray(bgr)
            rgb[0::3], rgb[2::3] = bgr[2::3], bgr[0::3]
            rows.append(bytes(rgb))
    finally:
        _gdi32.SelectObject(mem_dc, old)
        _gdi32.DeleteObject(bitmap)
        _gdi32.DeleteDC(mem_dc)
        _user32.ReleaseDC(hwnd, window_dc)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(_png(width, height, rows))
    return out
