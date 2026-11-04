"""Test automation for SFV Ember: deploy, launch, control, capture.

Shared by the command line (sfve.py) and the MCP server (sfve_mcp.py).
Windows only; standard library only.

Safety rules this module enforces:
* A copy is only launched with the proxy and mod in place, and is killed if
  its control pipe (which the mod opens only after the network guard is
  installed) does not answer within GUARD_TIMEOUT_S.
* Launches go straight to the exe; nothing here uses the game's online modes.
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import hashlib
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
GUARD_TIMEOUT_S = 20.0
STEAM_APP_ID = "310950"


class SfveError(RuntimeError):
    pass


# --- deploy -----------------------------------------------------------------

def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def deploy() -> list[str]:
    """Copies the built proxy and mod next to the game exe."""
    copied = []
    for name in MOD_FILES:
        src, dst = BUILD_DIR / name, GAME_DIR / name
        if not src.exists():
            raise SfveError(f"{src} is not built")
        if not dst.exists() or _sha256(src) != _sha256(dst):
            shutil.copy2(src, dst)
            copied.append(name)
    return copied


def undeploy() -> list[str]:
    """Removes the proxy and mod from the game folder (logs stay)."""
    removed = []
    for name in MOD_FILES:
        path = GAME_DIR / name
        if path.exists():
            path.unlink()
            removed.append(name)
    return removed


def deployed_matches_build() -> bool:
    return all((GAME_DIR / n).exists() and _sha256(GAME_DIR / n) == _sha256(BUILD_DIR / n) for n in MOD_FILES)


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


def launch(index: int, mode: str = "probe", extra: list[str] | None = None, windowed: bool = True) -> Instance:
    """Starts one copy and waits until its guard is confirmed (or kills it)."""
    if not deployed_matches_build():
        raise SfveError("the game folder does not hold the current build: run deploy first")
    log_path(index).unlink(missing_ok=True)  # each run starts its own log
    args = [str(GAME_EXE), f"-sfve_instance={index}", f"-sfve_mode={mode}"]
    if windowed:
        args += ["-windowed", "-ResX=960", "-ResY=540"]
    args += extra or []
    # Steam sets these when it starts a game; its DRM wrapper exits (0x35) without them.
    env = dict(os.environ, SteamAppId=STEAM_APP_ID, SteamGameId=STEAM_APP_ID)
    proc = subprocess.Popen(args, cwd=GAME_DIR, env=env, creationflags=subprocess.DETACHED_PROCESS)
    try:
        reply = request(index, "status", timeout_s=GUARD_TIMEOUT_S)
    except SfveError:
        kill_pid(proc.pid)
        raise SfveError(f"instance {index} did not confirm its network guard within {GUARD_TIMEOUT_S:.0f} s; killed")
    if not reply.startswith("ok "):
        kill_pid(proc.pid)
        raise SfveError(f"instance {index} answered '{reply}'; killed")
    return Instance(index, proc.pid)


def kill_pid(pid: int) -> None:
    subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)


def kill_all(timeout_s: float = 15.0) -> list[int]:
    """Kills every copy and waits until they are gone (their pipes with them)."""
    pids = running_pids()
    for pid in pids:
        kill_pid(pid)
    deadline = time.monotonic() + timeout_s
    while running_pids() and time.monotonic() < deadline:
        time.sleep(0.2)
    return pids


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


def boot_to_menu(index: int) -> None:
    """From a fresh launch to the main menu: the offline notices take A twice."""
    time.sleep(20)
    focus(status_pid(index))
    request(index, "pad 0 A:150 -:4000 A:150 -:4000")
    time.sleep(12)


def fight(index: int) -> None:
    """From a fresh launch into a Player 1 vs Player 2 battle (fight on screen)."""
    boot_to_menu(index)
    run_route(index, VERSUS_FROM_MAIN_MENU)
    time.sleep(8)


# --- logs ---------------------------------------------------------------------

def log_path(index: int) -> Path:
    name = "sfv_ember.log" if index == 0 else f"sfv_ember.{index}.log"
    return GAME_DIR / "SFVEmber" / "logs" / name


def tail(index: int, lines: int = 40) -> list[str]:
    path = log_path(index)
    if not path.exists():
        return []
    return path.read_text(errors="replace").splitlines()[-lines:]


# --- window capture -------------------------------------------------------------

_user32 = ctypes.WinDLL("user32", use_last_error=True)
_gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
_EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
_user32.GetWindowDC.restype = wt.HDC
_user32.GetWindowDC.argtypes = [wt.HWND]
_user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
_user32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
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


def _png(width: int, height: int, rgb_rows: list[bytes]) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\x00" + row for row in rgb_rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def screenshot(pid: int, out: Path) -> Path:
    """Captures a game window (even when covered) to a PNG."""
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
