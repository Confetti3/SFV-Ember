"""Test automation for SFV Ember: deploy, launch, control, capture.

Shared by the command line (sfve.py) and the MCP server (sfve_mcp.py).
Windows only; standard library only.

This module is the one import name the runners use; it only re-exports the parts,
each a module of its own: sfvelib_config (folders, switches, SfveError, verdict),
sfvelib_pipe (requests over the control pipe), sfvelib_procs (processes, the run's
registry, kill_all, run_scope), sfvelib_launch (deploy, launch), sfvelib_logs (logs,
checkpoints, desync lines), sfvelib_windows (focus, close, screenshot, DPI) and
sfvelib_menu (menu routes, boot_to_menu). A test that replaces a function patches the
module that defines it and calls it (sfvelib_selftest.py does).

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
  network guard is installed) does not answer within GUARD_TIMEOUT_S. Through
  the launcher the clock has two phases: the launcher's own preparation (up
  to LAUNCHER_PREP_TIMEOUT_S) ends when the launcher has started the game, and
  only then does the GUARD_TIMEOUT_S guard deadline run.
* Nothing here uses the game's online modes.
* Only copies this module started are killed. A process is known by its id
  and its creation time together (a bare id can be reused), kept in the
  registry of the run that started it: a copy whose pipe answers is ours
  only when its process is the one just started or descends from it, and
  kill_all ends a registered process only while its creation time still
  matches. Steam is never ended: steam.exe, steamwebhelper.exe and
  SteamService.exe, and what they started, are outside every process tree
  this module ends, even when the launcher started them.
* Every process this module starts or ends belongs to a run, and the run is
  mandatory: registry_path() raises without one, so there is no shared
  default registry. A runner script sets SFVE_RUN (localstack.ps1,
  runroom.ps1) or a Python runner calls begin_run(); the two clients with no
  runner around them, sfve.py and sfve_mcp.py, call use_run("cli") and
  use_run("mcp"): SFVE_RUN when set, otherwise that fixed name. The run is
  never inferred from the caller's process ancestry, so it is the same
  however each command is started (a new shell per command included): a later
  `sfve.py kill` finds what an earlier `sfve.py launch` started, a restarted
  MCP server finds its predecessor's copies, and the CLI, the MCP server and
  each runner never meet. Two people or agents using the CLI on one PC share
  the "cli" run: give each its own SFVE_RUN. Helper processes of a run (a
  room supervisor and the room hosts it starts) are registered with track(),
  by id and creation time like the copies. A run that died is cleaned up
  with `sfve.py kill --all`.
* The game folder is only written in proxy mode, by deploy, which records
  what it installed in MANIFEST; undeploy removes only the files listed there.
"""

from __future__ import annotations

from sfvelib_config import (  # noqa: F401
    GAME_DIR,
    BUILD_DIR,
    GAME_EXE,
    LAUNCH_LOCK,
    LOCK_STALE_S,
    MOD_FILES,
    ASSETS,
    GUARD_TIMEOUT_S,
    LAUNCHER_PREP_TIMEOUT_S,
    STEAM_APP_ID,
    VIA,
    PACKAGE_DIR,
    LAUNCHER_FILES,
    LOG_DIR,
    MANIFEST,
    SfveError,
    runtime_dir,
    runtime_file,
    verdict,
)
from sfvelib_pipe import (  # noqa: F401
    pipe_path,
    request,
    request_ok,
    _reply_pid,
    status_pid,
)
from sfvelib_logs import (  # noqa: F401
    log_path,
    SET_ASIDE_KEPT,
    SET_ASIDE_WAIT_S,
    set_aside,
    tail,
    count_log,
    CHECKPOINT_LINE,
    GAME_GAP_FRAMES,
    DESYNC_PATTERN,
    _DESYNC_LINE,
    _REGION_LINE,
    log_lines,
    parse_checkpoints,
    compare_checkpoints,
    desync_lines,
    count_desyncs,
    region_hashes,
    unmatched_regions,
)
from sfvelib_windows import (  # noqa: F401
    _user32,
    _gdi32,
    _EnumWindowsProc,
    main_window,
    focus,
    close_window,
    _png,
    _dpi_aware,
    screenshot,
)
from sfvelib_procs import (  # noqa: F401
    Instance,
    running_pids,
    Identity,
    _kernel32,
    _ProcessEntry,
    _creation_time,
    process_identity,
    _Process,
    _SHARED_PROCESSES,
    _process_table,
    _process_tree,
    _terminate,
    begin_run,
    use_run,
    _registry_dir,
    _safe_name,
    registry_path,
    _read_entry,
    _registered,
    _remember,
    track,
    _kill_tree,
    _registries,
    surviving,
    kill_all,
    run_scope,
)
from sfvelib_launch import (  # noqa: F401
    take_launch_lock,
    release_launch_lock,
    _sha256,
    _deployed_files,
    _installed,
    _require_game_stopped,
    deploy,
    undeploy,
    deployed_matches_build,
    _game_options,
    launch,
    build_kind,
    refuse_player_build,
    launcher_log_path,
    _LogCursor,
    _await_game_start,
)
from sfvelib_menu import (  # noqa: F401
    VERSUS_FROM_MAIN_MENU,
    _MASH_DIRECTIONS,
    _MASH_BUTTONS,
    mash_steps,
    run_route,
    MAIN_MENU_SCENE,
    scene,
    boot_to_menu,
    fight,
)
