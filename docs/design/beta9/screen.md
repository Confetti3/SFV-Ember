# beta.9: Screen mode, the game's setting and Borderless (2026-11-13)

The beta.8 launcher always started SFV with `-windowed` (EMBER_LIBRARY.md section 36: SFV's
exclusive full screen changed the monitor's mode and upset a display), so SFV's own Screen Mode
option was ignored and a player who chose Full Screen still got a window.

**Owner's decision (2026-11-13).** "Make it so people can use the setting and add borderless."

## 1. The choice

One setting, Settings > Interface > Look > **Screen mode**, with three values. It applies at the
next game start, because the launcher decides the command line.

| Value | Launcher passes | Result |
|---|---|---|
| Game's setting (default) | nothing | SFV uses its own saved Screen Mode, exclusive full screen included |
| Window | `-windowed` | a window at the game's saved resolution, as every start was until beta.8 |
| Borderless | `-windowed -sfve_screen=borderless` | the mod takes the frame off the window and lays it over the whole monitor it is on |

A test's own `--game-option` `-windowed`, `-ResX=`, `-ResY=` or `-sfve_screen=` wins: the setting
adds nothing then (`screen::PlanFor`, `src/mod/ScreenMode.hxx`). `tools/sfvelib.py` always passes
`-windowed -ResX=N -ResY=N` (also on its direct, non-launcher route), and `runroom.ps1`,
`synctest.ps1` and every `run*.py` start games through it, so no harness changes. `SFVE_SCREEN=borderless`
adds `-sfve_screen=borderless` to a copy sfvelib starts, for a single-copy Borderless run.

## 2. Where it lives

- **The row** is the library's (`b9/lib-screen`, 8f56f3c25): `ShellView::offersScreenMode` and
  `screenMode`, `ShellAction::setScreenMode`, `ShellGame::SetScreenMode`, the same shape as the
  Background artwork row (3515bca6f). The shell has no hook for a game's own rows, so a library
  change was unavoidable. SF4 Ember never sets `offersScreenMode`, so its Look page is unchanged
  and its own Borderless stays a Player-page preference.
- **The preference** is `interface.json`'s `"screenMode": "game" | "window" | "borderless"` in
  SFV Ember's settings folder, next to `backgroundArtwork` (`src/ember/SfvScreenMode.cxx`). It is
  not a `PlayerPreferences` field: that struct is a wire format (golden tests, saved settings
  shared with SF4's schema). The launcher reads the file at each start (`ReadScreenMode`); the
  mod's row reads it once and writes it on a change (`ChooseSfvScreenMode`). A missing file or key
  is Game's setting; a bad value or a file that does not read is Game's setting with the reason in
  the launcher's log.
- **The launcher** (`SfvLaunch.cxx`, `StartGame`) logs `Screen mode: <mode>; window options
  passed: ...` and the whole command line. The mod logs `screen: ...` at start naming what it was
  given, so a window that is not what the player expected is explained by both logs.
- **Strings**: `settings.screen_mode`, `settings.screen_mode_detail`, `settings.screen_mode_game`
  and `settings.screen_mode_window` in all 14 catalogs; Borderless reuses `player.borderless`.

## 3. How SFV picks its window mode (read, not run)

- This PC's `%LOCALAPPDATA%\StreetFighterV\Saved\Config\WindowsNoEditor\GameUserSettings.ini`
  (read-only) holds, under `[/Script/KiwiGame.KiwiGameUserSettings]`, `FullscreenMode=2`,
  `LastConfirmedFullscreenMode=0`, `LastUserConfirmedResolutionSizeX/Y=0`, `WindowPosX/Y=-1`,
  `bUseVSync=True`. UE4's `EWindowMode` is 0 Fullscreen, 1 WindowedFullscreen, 2 Windowed.
  `FullscreenMode=2` next to a last confirmed 0 suggests that starts with `-windowed` wrote the
  windowed value over what the player had chosen. That would mean a player who had Full Screen
  before beta.8 may already have Windowed saved; Game's setting then shows a window until they
  choose again in the game's options. Not proven: it needs a start with the file watched.
- Not read from the exe: whether SFV's own options screen offers Borderless (UE4's
  WindowedFullscreen, 1) beside Full Screen and Windowed, and whether this UE4 build honours
  `-fullscreen`, `-ResX/-ResY` in full screen or `-ExecCmds="r.setres ...wf"` (a shipping build
  may drop console commands). Nothing in the repo's notes records any of these, and no launch was
  allowed here. This is why Borderless does not depend on the engine's own windowed-full-screen
  mode (option (b) of the brief): it cannot be chosen from the command line without those
  facts, and the launcher must not rewrite the player's ini.

## 4. Borderless: the mechanism and why

Chosen: **(a)**, the mod restyles the window the game already made.

1. The launcher passes `-windowed`, so the engine builds an ordinary windowed swap chain at the
   game's saved resolution. No exclusive full screen ever exists, so the monitor's mode is never
   touched.
2. The overlay's window procedure (`Overlay.cxx`, `OverlayWndProc`, on the window's own thread)
   answers a private message the render thread posts every 30th Present
   (`BorderlessPresented`). It reads the monitor from `MonitorFromWindow` +
   `GetMonitorInfoW`, and when the window has a frame, a border edge or another rectangle it clears
   `WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX` and the edge styles and calls `SetWindowPos` with
   the monitor's rectangle (`SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE`). It is not
   `TOPMOST`; a window that covers its monitor makes the taskbar give way when it is in front.
3. The same check runs every half second for the whole session, so it re-applies when the engine
   restyles the window (its own options, Alt+Enter), at most 4 times a second and 40 times, then
   stops and logs it rather than fight.
4. It does nothing while the swap chain is in exclusive full screen or the window is minimised.

Why this and not the alternatives: it needs nothing from the engine's command line or ini, it is
the same mechanism for any window size, and every step is a plain Win32 call whose result the log
shows. Threading: the render thread only reads and posts; `SetWindowLongPtr` and `SetWindowPos`
run on the window's thread, because from another thread they send messages into a window procedure
whose thread may be waiting on the render thread (the engine's flush).

**The back buffer.** The window's new size reaches the engine as `WM_SIZE`, which the engine
turns into a viewport resize and a `ResizeBuffers`. That is how a UE4 window follows a drag, but
whether this build's window is user-sizable and its viewport follows is not proven (list in 7).
If the engine does not resize, a windowed DXGI chain is stretched by the compositor to the window,
so the picture still covers the monitor, at the saved resolution and stretched (the wrong aspect
on a monitor that is not 16:9). The overlay logs every resize (`overlay: back buffers resized,
asked WxH, now WxH`), so the first run shows which it is. Passing the monitor's size as
`-ResX/-ResY` from the launcher was rejected: the launcher does not know which monitor the window
opens on (saved `WindowPosX/Y`), and in a DPI-unaware process the numbers are not the monitor's
pixels.

**DPI and monitors.** The monitor rectangle is read and the window placed on the same thread, in
that thread's DPI awareness, so the two always agree. A per-monitor-aware process gets physical
pixels; an unaware one gets the system's scaled picture of the same monitor, which still covers it
(the game then renders at the scaled size and Windows enlarges it). Whether SFV's exe is aware is
not known; the first apply logs the window's awareness and the monitor's real mode. The monitor is
the one the window is mostly on, so a game opened on the second monitor is laid over that one.

## 5. Exclusive full screen under "Game's setting"

Every place that assumes a windowed swap chain, and what was done:

- `Overlay.cxx` `FindSwapChainFunctions` sets `desc.Windowed = TRUE` on a throwaway swap chain over
  a hidden `STATIC` window, only to read `Present`'s and `ResizeBuffers`' addresses from its vtable.
  It is not the game's chain and does not interact with the mode. Comment added.
- `Draw` takes the size from `swap->GetDesc()` (`BufferDesc`, valid in either mode) and
  `h_ResizeBuffers` drops the render target before every resize, which DXGI's mode switch
  (`SetFullscreenState` -> `ResizeTarget` + `ResizeBuffers`) goes through, so the overlay follows a
  switch without change. `ResizeTarget` and `SetFullscreenState` are not hooked and need not be.
- `IsGameWindow` needs a visible, top-level window of this process: true in exclusive full screen
  and while minimised after Alt+Tab.
- `BackgroundPlay` (the player's Play in the background, and `-sfve_background=1`) swallowed
  every focus-loss message so the engine never heard of it. In exclusive full screen Windows takes
  the chain out of full screen on a focus loss, and an engine that did not hear of it would keep
  drawing into it. `Draw` now publishes `swap->GetFullscreenState` (`mod/WindowState.hxx`) and the
  filter lets the focus loss through while it is exclusive: Play in the background then behaves as
  the library's own help text says ("fullscreen still minimizes").
- `WM_ACTIVATEAPP` (`s_focused`) is read as before.
- `FrameGrab` (photo booth, tests' screenshots) copies from `GetBuffer(0)` and never reads the
  window's size; the tools' windowed copies are unchanged. It was not changed or run.
- The `-sfve_background=1` test option is only meaningful windowed, and every harness passes
  `-windowed`.

## 6. Tests written (not run)

`tests/ScreenModeTests.cxx` (names and numbers, the launcher's options for each mode, a test's
window option beating every mode, the saved choice round trip and its bad inputs, Borderless's
style and rectangle rules, the re-apply limit), `tests/BuildKindTests.cxx` (a player build reads
`-sfve_screen=borderless` and nothing else of it), and the library's `ScreenModeRow` in
`shell_settings_journey_test.cxx` (row order, values, stepping, no action without a change, no
row for a game that does not offer it).

## 7. Verify in the final round (laptop, one launch each)

For each mode check the launcher log's `Screen mode:` line, the mod log's `screen:` line and the
command line.

1. **Game's setting**, with the game's own option on Full Screen: exclusive full screen opens, and
   the Ember menu (F10 / Start) opens and closes; Alt+Tab out and back still gives a picture and a
   menu; `overlay: back buffers resized` lines are sane; screenshot capture still works. Then with
   Windowed saved in the game: a window, as before. Also whether a start under Window since beta.8
   already overwrote the saved mode (watch `GameUserSettings.ini`'s `FullscreenMode` across a
   Window start and an exit; copy it first).
2. **Window**: a framed window at the saved resolution, `-windowed` on the command line.
3. **Borderless**, on the primary monitor at 100% and, on the laptop, at 150%: no title bar or
   border (`style` in the log has no `00C40000`), window rectangle equals the monitor's, the
   monitor's mode unchanged (the log's `mode WxH` equals the desktop's and nothing flickers), the
   taskbar is hidden and does not flicker, the menu opens and closes and its mouse hits line up,
   Alt+Tab and back keeps the borderless window, a drag of the window by the engine does not
   happen. Read the log for `back buffers resized, asked ... now ...`: whether the engine resized
   to the monitor (best) or the compositor stretched the old size (acceptable, wrong aspect on a
   non-16:9 monitor). Read the window awareness the first apply logs (per-monitor, system or
   unaware). Press Alt+Enter and open the game's own options and change its Screen Mode while
   Borderless is chosen: the window should come back to Borderless within a second, up to 40 times.
   With a second monitor, start the game there.
4. **A two-copy test**: `tools\runroom.ps1` (or `sfve.py`) with the setting on Borderless in
   `interface.json`: both copies still get their windowed sizes (`-windowed -ResX -ResY` in each
   launcher log, `window options passed: none`, mod log `Window (-windowed)`), and with
   `SFVE_SCREEN=borderless` one copy goes borderless.
5. With **Play in the background** on and Game's setting on Full Screen: Alt+Tab out must not leave
   a frozen or black game on return.

## 8. Not done

No build, no ctest, no game launch, no run of the library's tests. The window's behaviour under
the engine (resize, restyle, DPI) is argued from Win32 and UE4's general behaviour, not observed.
The exe's manifest (DPI awareness) and its Screen Mode options were not inspected.
