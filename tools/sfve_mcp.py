"""SFV Ember test driver as an MCP server (stdio).

Registered in the repository's .mcp.json; run through uv so the mcp package
needs no global install:  uv run --with mcp python tools/sfve_mcp.py
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402
from mcp.server.fastmcp import FastMCP, Image  # noqa: E402

mcp = FastMCP("sfve")
# This server's own run (sfvelib module docstring): named after the MCP client
# that started it, so a restarted server still finds, and stop_all still ends,
# the copies its predecessor launched; the command line and runners never share it.
lib.begin_session("mcp")


@mcp.tool()
def deploy() -> str:
    """Copy the built proxy and mod into the game folder."""
    return f"copied: {lib.deploy() or 'nothing (already current)'}"


@mcp.tool()
def launch(index: int, mode: str = "probe", extra_args: list[str] | None = None) -> str:
    """Start one SFV copy (-sfve_instance=index) and confirm its network guard; it is killed otherwise."""
    inst = lib.launch(index, mode, extra_args)
    return f"instance {inst.index} pid {inst.pid}: guard confirmed"


@mcp.tool()
def control(index: int, command: str) -> str:
    """Send one control-pipe command (status, virtual, pad, synctest, ...) to a running copy."""
    return lib.request(index, command)


@mcp.tool()
def versus(index: int) -> str:
    """From the main menu, play the menu route into a Player 1 vs Player 2 battle."""
    lib.run_route(index, lib.VERSUS_FROM_MAIN_MENU)
    return f"instance {index}: route to versus played"


@mcp.tool()
def screenshot(index: int) -> Image:
    """Capture a running copy's window."""
    pid = lib.status_pid(index)
    out = Path(tempfile.gettempdir()) / f"sfve-shot-{index}.png"
    lib.screenshot(pid, out)
    return Image(data=out.read_bytes(), format="png")


@mcp.tool()
def log_tail(index: int, lines: int = 40) -> str:
    """Last lines of a copy's mod log."""
    return "\n".join(lib.tail(index, lines))


@mcp.tool()
def stop_all() -> str:
    """Kill every running SFV copy."""
    return f"killed: {lib.kill_all()}"


if __name__ == "__main__":
    mcp.run()
