# Photo booth in the mod (stream A-mod)

Branch `b8/booth` (off `b8/base`, with `b8/desync` merged in for its Tiny fighters change). The
pipe contract is `booth-protocol.md`; this file says how the mod meets it, where it goes further
than the protocol says, what is not verified, and what the first pilot run must check.

## Mode

`-sfve_mode=booth` makes `ModMode::Booth` in a development build only: a player build reads
`-sfve_mode` only as `netplay` (`ModConfig.cxx` `kPlayerOptions`), so `booth` is ignored and noted
in the log like any test option (`BuildKindTests`). `MakeMode` returns `BoothMode`. The mode does
not roll back, so the projectile release hook is not in its battle set (`RollsBack`). It installs
no shell, so the overlay sets no interface and `Overlay.cxx`'s `Draw` returns before drawing
(`if (!ui.draw) return;`, the only `SetOverlayUi` caller being the netplay mode's room shell): the
back buffer at Present is the game's alone.

`BoothMode::Install` allows the booth scale (`SetFighterScaleAllowed`), sets the present tap to
`grab::OnPresent`, installs the depth probe, and registers the `booth` command and status field.

## Stepping and holding

Each rendered battle frame (`BattleDriver` hands it over once bound) the mode either advances the
battle by one `SfvAdapter::Advance` with its own inputs or does nothing (held), as `NetplayMode`'s
`freeze` does; the game keeps presenting either way.

- `NewBattle`: the battle number, fight frame -1, hold target = a hold asked between battles (else
  0), booth scale and remembered parts forgotten, `SetPracticeGauges` (logged; `gauges` in the
  status).
- Inputs: the skip bit (0x4000) on both sides while the flow is IntroDemo (the intro ends on it
  when the battle data's intros are skippable; the bind's log line says whether they are); a
  `play`'s steps for its side; nothing otherwise. The PC's own pads never reach a booth battle
  once it is bound (the session input source).
- `fightFrame` becomes 0 on the first frame the flow announces Fight and counts every advance
  after that (also if the flow later leaves Fight).
- Held while `fightFrame >= holdAt`. **With no command the battle holds at fight frame 0.**
- `hold F`: in a battle, `F` below the current fight frame is `error frame F is past`; otherwise
  the target (a running `play` is dropped). Between battles (before the first bind, or after
  `leave`) it is kept for the next battle.
- `run n`: target = fight frame + n. `play s steps`: target = fight frame + the steps' frames,
  the steps played from the current frame. Both: `error no battle`, `error the fight has not
  started`, `error a play is running`.
- `leave`: the battle stands still from the next frame; the booth scale and the HUD movie are
  forgotten and `mod::LeaveBattle()` is asked every frame until it takes (as `NetplayMode::Leave`),
  from the battle frame, not from the game-thread queue (whose work must not end a battle).

All of this is bookkeeping under the mode's mutex; the pipe thread never touches the game.

## Scale (`booth scale`, TinyFighters)

- `TinySides` gained a per-side `factor` (default `kTinyScale`); `tiny::ScaledScale` and
  `ScaledTransform` take a factor (default `kTinyScale`), `ApplyToPart` uses the side's. The Tiny
  tests are unchanged and pass as they were written.
- While the booth allows it, the per-frame hook (`h_VisualUpdate`, rva 0x3A06C0) remembers each
  side's `CharaVisualPart` (side read as before: the owner's weak pointer and use count,
  `CharaUnit+0x758`). It remembers before the fight-update check, as the per-frame update may run
  inside the battle update.
- `ApplyFighterScaleNow(p1, p2)` (game thread, checked with `GameThreadId`, through the pipe's
  wait on `RunOnGameThread`): refused for a factor that is not positive and finite, outside the
  booth, when the picture's hooks are not in, or before both parts are known. Otherwise the
  factors are stored per side and both meshes get `SetWorldScale3D(wanted * factor)` at once; the
  scaled actors are noted so the whole-mesh transform hook (Critical Art cinematics) scales them
  too.
- A stored factor makes the side count in `TinyFightersNow()` (factor wins over a Tiny pick), so
  the desync stream's `BeginFightUpdate`/`EndFightUpdate` give the fight the normal-size model
  around every booth `Advance` and the picture the booth size after it. The factor stays until
  the next battle or `leave` (`ForgetFighterScale`).
- Netplay is untouched: nothing remembers parts or stores a factor unless `BoothMode::Install` ran,
  and `ClearTinyFighters` keeps a booth side's actor only while a booth factor is set.

## Frame grab (`overlay/FrameGrab`)

- Present tap (render thread, before the overlay's draw): only the game window's swap chain
  (pid, visible, root window, as the overlay's `IsGameWindow`) counts as a present.
- A grab is due at the present `after + 1` presents after `QueueGrab`. At that present the back
  buffer (`GetBuffer(0)`) is copied to a staging texture and mapped (the `ReadFaceSquare`
  pattern), rows packed; the BMP (BITMAPV4HEADER, top-down, BI_BITFIELDS BGRA masks, alpha kept,
  sRGB) and the depth files are encoded and written on a thread of their own. `pending` stays
  true until the files are written. Formats: R8G8B8A8 / B8G8R8A8 (UNORM, sRGB, typeless) and
  R10G10B10A2 (UNORM, UINT, typeless; 10-bit channels rounded to 8, 2-bit alpha x85). Any other
  format, or a multisampled back buffer, fails the grab with the reason.
- Depth probe (booth only): a throwaway device's immediate context gives the vtable;
  `OMSetRenderTargets` (33), `OMSetRenderTargetsAndUnorderedAccessViews` (34) and the seven draw
  calls (12, 13, 20, 21, 38, 39, 40, d3d11.h's order) are hooked as one set. Only calls on the
  game's immediate context (read from the swap chain's device) count. Each bound depth-stencil
  view's texture is a target (held by reference for a frame), with its draws. At each present the
  frame's list becomes the report (`booth depth`) and a new frame starts with the bound target.
- `depth=auto` takes the target with the most draws among those of the back buffer's size and
  single-sampled, else the most draws of all (a shadow atlas may out-draw the scene); `depth=<n>`
  the n-th by first use. The texture is copied to staging in its typeless family (D32 ->
  R32_TYPELESS, D24S8 -> R24G8_TYPELESS, D32S8X24 -> R32G8X24_TYPELESS, D16 -> R16_TYPELESS) and
  written as one little-endian uint32 per texel, rows packed: float bits (D32, and D32S8X24's
  depth word), depth in the low 24 bits with stencil in the top 8 (D24S8), zero-extended (D16).
- If the probe cannot be hooked, grabs have colour only and `depthProbe` is false.

## HUD probe (HudText)

- The hook remembers the movie that last named a plate or marker (`SetPilotName`,
  `SetCursorText`: certainly the HUD movie; the marker is named at each round's start, so before
  fight frame 0). `ForgetHudMovie` at `leave` and at a new battle that the booth did not leave.
- `SetHudCallLog(on)` logs every invoke (method, number, text, movie).
- `HudCall` (game thread, checked) calls the original invoke (rva 0x73D600) on that movie with
  mod-owned FStrings; no text is UE4's empty FString (null, 0, 0).
- The game's own callers of the invoke (21 functions; strings read from the laptop database) use
  these methods: `Setup`, `SetFaceIconPath`, `SetPlayerName`, `SetPilotName`, `SetTitlePlate`,
  `SetRanking`, `SetSurvivalTime`/`Stage`/`Score`, `SetAbility`, `SetExtraRecordText`,
  `SetExtraRecordLabelText`, `SetExtraTopRecordText`, `SetArcadeScoreStr`, `SetArcadeScore`,
  `SetArcadeSpecialStage`, `SetArcadeStage`, `SetBonusScore`, `SetBonusStageStr`,
  `SetCursorColor`, `SetCursorText`, `ShowCursor`, `HideCursor`, `SetCursorPosition`,
  `SetTrialStage`, `SetWins`, `SetLeagueIcon`, `SetVisiblePlayerInfo`, `SetSTAGETYPE03Icon`/`Id`,
  `SetVisibleSTAGETYPE03Icon`/`Id`, `SetText`, and the results/pause movies' own. None is an
  obvious "hide the HUD"; `SetVisiblePlayerInfo 0` and `HideCursor` are the first to try. The HUD
  disappears during Critical Art cinematics without any of these, so the hide is probably not an
  invoke (the movie's own visibility): `booth hudlog on` across a Critical Art settles it.
  Fallback (plan): walk both fighters inward (`booth play`) so they clear the gauges.

## `launch` status field (VersusLaunch)

New (there was none): `{"number","state","loaded","asked","looks","mismatch","failure"}`.
`number` and `state: "asked"` are set synchronously by `QueueLaunch`/`LaunchOnMainMenu` (so by the
time the pipe answers `ok queued`), then `jumping`, `loading` (with `asked`: the looks written into
the jump), `loaded` (with `looks` as the battle setting holds them, read where the log line
"player N loaded ..." is written) or `failed` with `failure` (refusals, jump failures, load
timeouts, a state other than a battle). `mismatch` compares fighter, costume and colour (not the
voice); an unreadable look counts as a mismatch.

## Also on this branch

`content` (development): the battle setup asset's select-grid list (`+48`), one line per entry in
order. IDA (laptop, rva 0x55A9B0): with `+96` clear the list is handed to the array copy rva
0x5156A0, whose element copy rva 0x4E1F30 walks 16-byte `FString`s: the entries are fighter codes
only, with no slot, row or column fields.

## Not verified (no build or game run on this branch yet)

- Compiles /WX on MSVC; the tests (PhotoBoothTests, TinyFightersTests, BuildKindTests) pass.
- That a held battle presents a stable frame (no camera drift, no idle animation: the battle's
  update is not called, but the engine's own presentation may animate cloth, particles or the
  stage).
- That the intro ends on the skip bit in a pipe launch's offline versus (`intros skippable` in
  the bind log line).
- That `CharaVisualUpdate` runs for both sides so the parts are known before fight frame 0, and
  that `SetWorldScale3D` on a frozen frame shows by the next presents (the driver waits 12).
- That 0.001 hides a fighter fully (shadow, effects and items attached to the fighter may stay).
- The context vtable slots and that the game's draws go through the immediate context on the
  thread that presents (UE4's D3D11 RHI); that the scene depth is still intact at Present (not
  reused by post-processing or the HUD); its format and whether depth is reversed (UE4: near 1,
  far 0).
- The back buffer format and whether its alpha means anything (the BMP keeps it as it is).
- The Steam overlay's toasts: if Steam's Present hook runs before the mod's, they are in the
  grab. Run the booth with the Steam overlay off.
- The HUD movie pointer and every `booth hud` call; whether any invoke hides the HUD.
- The `launch` field's states over a real launch, and `mismatch` for a costume this copy lacks.

## What the first pilot run must check

1. `status`: `mode` "booth", `booth` null, `battleHooks` ready; log lines "booth: the photo booth
   is on" and "grab: the context's render targets and draws hooked".
2. `launch versus RYU:0:0 KEN:0:0 TRN 1 inf` -> `launch.state` loaded, `mismatch` false;
   `booth.battle` 1, `gauges` true, `fightFrame` 0, `held` true, the intro skipped.
3. `booth depth` over a held frame: the scene target at 1920x1080 with the most draws; the
   counts steady from present to present.
4. `booth grab A.bmp`, `booth scale 0.001 0.001`, wait 12 presents, `booth grab B.bmp`: A shows
   both fighters without any Ember drawing; B the same plate without them; `A.bmp.depth` differs
   from `B.bmp.depth` exactly on the fighters. Take one more grab after 60 presents and compare
   with B pixel by pixel (stability of a held frame).
5. `booth scale 1 1`, `booth play 0 RIGHT:40`, `booth run 30`: P1 walks right; the held frame
   after is stable; `booth play 0 MP+MK:1` and `booth run 30` for a V-Skill picture.
6. `booth hudlog on`, a Critical Art (`booth play`), `booth hudlog off`: what the HUD is told;
   `booth hud SetVisiblePlayerInfo 0` and `booth hud HideCursor 0` on a held frame.
7. `booth leave` -> the main menu; a second launch with `booth hold 120` sent before it binds:
   the next battle holds at 120.
8. Several copies at once (`-sfve_instance`): grabs from each land in their own files.
