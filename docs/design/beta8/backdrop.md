# beta.8: the menu's artwork backdrop (2026-11-12)

**The ask.** After testing beta.8: "the menu looks good, the dark background on all pages is
starting to bug me. Look to see if there is art in the game you could use that cycles for it."

## Why the pages were flat

The shell already had a backdrop: every full-window page draws `ui::DrawMenuBackdrop()` (the
library's `GameMenu.cxx`), the product's `menuBackdrop` picture cover-fitted to the screen under a
left-heavy ink tint, switched off per page by `GameMenu::backdrop` (see-through pages and the
post-match prompt, `ApplicationShell.cxx`; the recovery menu). SFV Ember's picture,
`assets/brand/sfv-ember-menu-background.png`, is loaded and drawn, but it holds no picture:
`scripts/build-sfv-brand.py` writes it as a 2048x1152 fill of the theme's ink (`GROUND = 100F0D`,
"the menu's ground: flat ink, no picture", since the Band theme's commit). Flat ink under an ink
tint is the near-black every page shows. Nothing failed to load and the tint is not the cause; a
picture that did not load would look the same, as the window's own ground is that ink at 97%.

## What it does

SFV's own full-screen illustrations behind every page of Ember's shell: Home, Online play, the
room, the fighter board, the galleries, the loadout and stage cards, Settings, Profile, Replays,
Training select. One layer for all of them, so moving between pages never restarts or changes the
picture.

- **The cycle.** A picture holds for 25 s on screen, then cross-fades (1.2 s, smoothstep) into the
  next. The order is shuffled per session (a fresh shuffle each round, never the same picture twice
  in a row, and every picture once a round). The clock only runs while the backdrop is drawn: a
  battle, the game's own menus or a closed shell do not use up a picture's time. A frame moves the
  clock at most 0.25 s, so a hitch never skips a fade. The first picture fades in from the plain
  ground. No pan or zoom.
- **The scrim.** The theme's ink over the picture, by kind of page (`SfvBackdropRules`'s
  `PageKindOf`, by the shell's screen):

  | page | screens | ink at top-left, top-right, bottom-right, bottom-left |
  |---|---|---|
  | column | `home`, `online`, `training` | 93%, 60%, 77%, 96% |
  | dense (every other) | the room, `selection` (board, galleries, cards), settings tabs, profile, replays... | 84%, 84%, 89%, 89% |

  A column page keeps the old tint's shape (its rows on the left stay on near-ink; the art shows on
  the open right; the bottom right a little darker for the hint bar's status). A dense page is even
  and heavy: the picture is a quiet texture between cards. Cards and panels keep their own fills
  (`v2::CardGround` is 92% opaque). A change of kind eases over 0.35 s.
- **The pictures' tone.** Drawn through a warm grey multiply (`kArtTint`, 240/232/216), so a cold
  illustration sits on the gold-on-ink ramp with the rest of the page. Set it to white to drop it.
- **The fit.** Cover: the picture keeps its shape and fills the screen. On a narrower screen (16:10,
  4:3) the full height and a centred band of the width; on a wider one (21:9) the full width and a
  band of the height placed 40% down the excess (faces sit high in these illustrations).
- **The setting.** Settings > Interface > Look > Background artwork, On by default, Off is the
  plain ground exactly as before. Saved as `"backgroundArtwork"` in `interface.json` in SFV Ember's
  settings folder (`%APPDATA%\SFVEmber`), as the hitbox viewer keeps its choice in `training.json`:
  it is SFV's choice, not one of the library's `PlayerPreferences`, so SF4's saved settings never
  see it. Turned off, both pictures are let go at the next frame.

## The art (`data/menu-art.json`)

Embedded in the mod as `data/select-grid.json` is (`sfve_embed_text_file`), so the package gains
no file and its inventory is unchanged. Families, each with `use` and its paths (under `/Game/`,
no extension; the object path is `/Game/<path>.<name>`):

| family | pictures | use | why |
|---|---|---|---|
| `gallery-special` | 181 | on | the gallery's guest-artist illustrations: made to be looked at whole |
| `gallery-ending` | 131 | on | the arcade endings, one or more per fighter |
| `gallery-mode03` | 247 | off | `GameMode/MODE03/Image/Gallery/Illust/gch_sfN_NNNN`: unknown until seen |
| `story` | 664 | off | `Chara/<CODE>/IllustDemo/Image`: story panels may be letterboxed, carry text, or be framed for the story's own layout |

Every one is a 1920x1080 uncompressed texture by the archive listing. Curating after the laptop
check is an edit of this file: flip a family's `use`, delete a path, or move paths into a new
family; the tests check the form of every path and that no path is listed twice. The title screen's
textures (`UI/TitleScreen/Textures`) are effect layers (noise, halos, the logo), not pictures, and
`Stage/<code>/UI/StageSelectBG` is 2048x512, too wide to cover a screen: neither is used.

**Context left out.** Showing the focused fighter's ending art on the board after a pause was
considered and left out: the board's own art pane already shows that fighter large, a backdrop
that follows the focus doubles the motion on the busiest page, and every change is a synchronous
load on the game thread while the player is moving through the board.

## Loading, lifetime and the game's garbage collection

- **Where and when.** On the game thread (`SfvBackdrop`'s loader, each game frame), only at the
  game's main menu scene, only while `IsAsyncLoading` (`mod::GameIsLoading`, `sfv::kIsAsyncLoading`)
  is false, only while the backdrop was drawn within the last second (so never in a battle, a
  watched match, a replay, a loading screen, the WIN, or with the shell closed), and only after a
  fade has settled for 2 s. One load at a time. The shell allows drawing only while it is shown over
  `MenuContext::MainMenu` with no match, no watched game and no replay playback (`SfvShell`'s
  `BackdropFrame`); see-through pages and the post-match prompt never draw it (`menu_.backdrop`).
- **A load stalls the game thread** (`StaticLoadObject` is synchronous, as for every picture SfvArt
  loads). It happens while the picture on screen is static and nothing fades; the laptop report
  gives each picture's load time (`load_ms`).
- **Two alive at most.** The cycle holds the one shown and the next; the one faded out is let go at
  the end of the fade, before anything is drawn in the next frame (ImGui's draw list of the frame
  that drew it has been rendered; Direct3D keeps a released resource alive for work in flight).
- **Let go when not shown.** Once the backdrop has not been asked for in 5 s (`kIdleReleaseSeconds`:
  the shell closed, a battle, a page without the backdrop), both pictures are let go (about 8 MB of
  GPU memory each), so a match holds none. Back at the menu the next picture loads once the game has
  finished loading and fades in from the plain ground over 1.2 s: no pop, a plain ground for a moment
  and one load's stall (about 17 ms, at most about 115 ms) shortly after the menu returns. Making the
  constant very large keeps them instead.
- **When it was asked for and drawn** (`DrawWatch`): kept on the presented frames (counted each
  frame before the shell draws) and the wall clock, never on the cycle's own clock, which stops while
  nothing is drawn. The status's `drawn` is drawn in the frame just presented; `hasPicture` is a
  picture ready to show; `loads` counts the loads started. The loader needs the frame just presented
  to allow the backdrop and the backdrop to have been asked for within 1 s of the wall clock.
  A picture turned off while still loading is refused when it arrives and let go; nothing else loads
  until it has (`Cycle::Alive` counts it).
- **Garbage collection.** As SfvArt does: the game's `UTexture2D` is used only until its Direct3D 11
  texture is taken (`mod::TakeGameTexture`, which checks the resource is the texture's own, the
  vtable is Direct3D 11's and the whole picture is resident) with an `AddRef` of ours; the pointer to
  the game's object is then dropped. Nothing roots the object, so the game's collector takes it at
  its next pass; the pixels stay alive through our reference until we release it (view, then
  texture). The window between the load and the take (a frame or two, at most 120) is SfvArt's too.
- **Skipped.** A path the game has no texture for, one whose resource never comes, one smaller than
  640x360 or whose view cannot be made: skipped for the session, the next tried after 1 s. If none
  loads, the page is plain, as before.

## The library (b8/lib-backdrop, two commits, additive)

1. `ui::SetMenuBackdropDrawer(MenuBackdropDrawer)` (`GameMenu.hxx/.cxx`): `DrawMenuBackdrop()`
   first asks the drawer, over the whole viewport into the menu's window; true stands alone, false
   leaves the product's art and tint. Unset (SF4, the lab, recovery) the function is unchanged.
2. Look's row: `ShellView::offersMenuArtwork`, `menuArtworkShown` (the game fills them in
   `FillView`), `ShellAction::showMenuArtwork`, `ShellGame::ShowMenuArtwork(bool)` (default
   false); `ApplicationShell::HandleAdjust` asks the game on Left/Right when the value changes;
   `ShellHost` routes the action to the game only where the view offered the row. Strings
   `settings.menu_artwork` and `settings.menu_artwork_detail` in all 14 catalogs. SF4 Ember offers
   no row: its Look is TV mode alone, as before.

Tests: `menu_dialog_test.cxx` `BackdropDrawer` (asked once per menu frame, the whole screen, not for
a menu without its backdrop or a flyout, unset is as before); `shell_settings_journey_test.cxx`
`MenuArtworkRow`.

## Verification hooks (development builds)

- Status field `backdrop`: enabled, allowed (this frame), drawn (in the frame just presented),
  hasPicture, loads, current, next, nextReady, fading, held, alive, failed, entries, lastLoadMs,
  exhausted, page (column or dense) and the report's progress. Until this fix `drawn` meant a picture
  was ready (the cycle's frame not empty), so it stayed true in a battle with a picture held.
- Pipe `backdrop step`: fade to the next picture as soon as it has loaded. `backdrop on|off`: the
  setting, saved as the row saves it. `backdrop report [sheets|plain] <folder>`: every path of the file,
  used or not, loaded in turn at the main menu (one every other frame while the game loads nothing;
  the cycle waits), `<folder>\backdrop-report.tsv` (index, family, use, path, result, width,
  height, DXGI format, load ms, sheet, cell), and with `sheets` `backdrop-sheet-NN.bmp`, 12x12 tiles
  of 128x72, each picture as Home shows it (cover fit, tint, Home's scrim); with `plain` the same
  tiles as the pictures are (cover fit only), to judge the families that are off. Each picture is let go
  as soon as it is sampled; nothing is written anywhere else. The pipe of a player build answers
  only ping and status, and the report refuses outside a development build.
- `tools/runbackdrop.py`: one copy; `--report [--sheets]` runs the report and prints what loaded,
  at which sizes, by family, the slowest loads and every used path that did not load (sheets
  converted to PNG where Pillow is installed); `--shots` the pages, stepped between shots.
  `tools/runshell.py --step-backdrop` steps it before each of its shots (the room pages).

### Laptop results (2026-11-12, ff1ca44 on 3515bca6f)

Both branches built, 8 of 8 test suites passed. 1223 of 1223 paths loaded: 1084 at 1920x1080, 139 at
1920x1106, all format 90 (B8G8R8A8_TYPELESS); a load about 17 ms, the slowest 114.5 ms. Every page
shot as intended; `runshell.py --step-backdrop` passed. The battle check failed on a stale `drawn`
(see above), fixed with the idle release and a stricter battle check.

### Laptop commands

From the sfv-ember checkout, a development build of `b8/backdrop` on the library's
`b8/lib-backdrop` (`scripts\build.ps1 -EmberRoot <that checkout>`), the output outside the repository:

```
python tools\runbackdrop.py --out D:\sfv-re\backdrop-run --report --sheets --shots
python tools\runbackdrop.py --out D:\sfv-re\backdrop-plain --report --plain-sheets
python tools\runshell.py --shots D:\sfv-re\backdrop-run\room --step-backdrop
```

### What the run must show

1. The report: the used families' pictures load at 1920x1080, format 87 or 91 (B8G8R8A8) or 28/29
   (R8G8B8A8) (measured: all format 90, see above); `load_ms` small enough that a load once every 25 s is not felt (look at the
   slowest); which paths fail and why.
2. The sheets: which pictures work as wallpaper under Home's scrim. Curate `data/menu-art.json`
   from them (and decide `gallery-mode03` and `story`).
3. The shots: every page as readable as on the plain ground (cards, headers, the hint bar), a
   different picture on each, Look's row On/Off, Off exactly the old plain ground, nothing in the
   Training battle (`bd-14`: not allowed, not drawn, no load, no more pictures held, none after
   5 s), and the room pages (`runshell.py`).
4. The log: `backdrop: <path>: loaded in N ms` only at the main menu, never during a battle; after a
   fade the old picture is let go (`status.backdrop.alive` stays at 2 or less).
5. Watch a fade in the game: about a second, no flicker, no hitch at its start.

## Not changed

Presentation only: the room build id, the layout id and the match version (`kMatchBuild`) are
untouched; the pick space and the select grid are untouched. The packaged menu picture stays (the
library's fallback while nothing has loaded, and with the setting off).
