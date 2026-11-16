"""sfvelib: a copy's window: focus, close, and a DPI-aware screenshot."""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import struct
import zlib
from pathlib import Path

from sfvelib_config import SfveError


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
