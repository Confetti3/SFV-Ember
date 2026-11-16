"""sfvelib: the folders, switches and timeouts every part shares, the one error, and the runners' verdict."""

from __future__ import annotations

import os
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
# The launcher's preparation before the game exists (the exe's build check
# and the game's start; it checks nothing of Steam's).
LAUNCHER_PREP_TIMEOUT_S = 30.0
STEAM_APP_ID = "310950"
VIA = os.environ.get("SFVE_VIA", "proxy")
PACKAGE_DIR = Path(os.environ.get("SFVE_PACKAGE_DIR", str(BUILD_DIR)))
LAUNCHER_FILES = ("Launcher.exe", "sfv_ember.dll", "sf4-net.exe")
# The mod's logs (mod/Log.cxx) and the launcher's, however the game started.
LOG_DIR = Path(os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))) / "SFVEmber" / "logs"
# What deploy installed in the game folder (proxy mode), relative to it.
MANIFEST = "sfve-deployed.json"


class SfveError(RuntimeError):
    pass


def runtime_dir() -> Path:
    """Where a copy loads its runtime files from, by the one mode switch: the
    package or build folder through the launcher, the game folder (where
    deploy puts them) in proxy mode."""
    return PACKAGE_DIR if VIA == "launcher" else GAME_DIR


def runtime_file(name: str) -> Path:
    """The runtime file `name` (a helper such as sf4-net.exe) in runtime_dir(),
    which must exist."""
    path = runtime_dir() / name
    if not path.is_file():
        where = "SFVE_VIA=launcher, SFVE_PACKAGE_DIR" if VIA == "launcher" else "SFVE_VIA=proxy, the game folder (deploy first)"
        raise SfveError(f"{path} is missing ({where})")
    return path


def verdict(failure: str | list[str], pass_text: str = "PASS") -> int:
    """The one verdict rule of the acceptance runners: prints PASS or
    FAIL (`failure`: a reason, or a list of them) and returns the exit code.
    A runner calls it after its cleanup, with a failure for every missing
    piece of evidence as well as for every difference found."""
    reasons = [failure] if isinstance(failure, str) else list(failure)
    reasons = [r for r in reasons if r]
    print("FAIL: " + "; ".join(reasons) if reasons else pass_text)
    return 1 if reasons else 0
