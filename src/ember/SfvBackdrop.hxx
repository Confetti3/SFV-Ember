#pragma once

// The menu's artwork backdrop (docs/design/beta8/backdrop.md): SFV's own
// full-screen illustrations behind every page of Ember's shell, one held for
// a while and then cross-faded into the next, under a scrim that keeps the
// page's text and cards as readable as on the plain ground. Its rules (the
// set, the cycle, the scrim, the fit) are SfvBackdropRules'; this runs them.
//
// - Loading: on the game thread, at the game's main menu only, while the
//   game is not loading anything (mod::GameIsLoading) and only while the
//   backdrop was on screen in the last second (so never in a battle, a
//   watched match, a replay, a loading screen, or with the shell closed).
//   One picture at a time, through the game (mod::LoadTexture), as SfvArt
//   loads the fighters' faces; its Direct3D texture is kept with a reference
//   of ours and the game's object is not (the game's garbage collection may
//   take it, the pixels stay ours until let go). At most two pictures are
//   alive, the one shown and the next; the one faded out is let go at once,
//   and both go once the backdrop has not been asked for in 5 s (the shell
//   closed, a battle). A picture that does not load, or loads smaller than
//   640x360, is skipped. When it was last asked for and drawn is kept on
//   the presented frames and the wall clock (backdrop::DrawWatch), never on
//   the cycle's clock, which stops while nothing is drawn.
// - Drawing: behind the shell's pages (ui::SetMenuBackdropDrawer), only
//   where the shell allows it this frame (BackdropFrame), on the render
//   thread. Where nothing has loaded yet, or the setting is off, the shell's
//   own plain ground shows, as before.
// - The setting: Settings > Interface > Look's Background artwork (the
//   library's ShellView::offersMenuArtwork), on unless the player turned it
//   off, saved as interface.json's "backgroundArtwork" in SFV Ember's
//   settings folder.
// - The pipe (development builds): `backdrop step` fades to the next picture
//   as soon as it has loaded, `backdrop on|off` sets the setting, and
//   `backdrop report [sheets|plain] <folder>` tries every path of the art
//   set (used or not) and writes backdrop-report.tsv there (path, loaded or
//   why not, width, height, format, load time), with `sheets` also small
//   contact sheets of the pictures as the main menu shows them, with
//   `plain` as they are (no scrim, no tint) (backdrop-sheet-NN.bmp, 144
//   tiles of 128x72 each). Nothing the report reads is kept. Status field
//   "backdrop": "drawn" is drawn in the frame just presented, "hasPicture"
//   a picture ready to show, "loads" the loads started this session.

#include <string>

namespace sfve::adapter {

// Once, from InstallEmberShell: the loader, the drawer, the pipe's command
// and the status field.
void InstallSfvBackdrop();

// Render thread, every frame, before the shell draws: whether the backdrop
// may show now (the shell shown over the game's main menu, with no match,
// watched match or replay), and the shell's screen, whose kind of page sets
// the scrim. Lets go of what is no longer shown, before anything is drawn.
void BackdropFrame(bool allowed, const std::string& screen);

// The player's choice, read from interface.json the first time it is asked.
// Any thread.
bool SfvBackdropShown();
// Look's Background artwork row: turns the backdrop on or off and saves the
// choice. False, the choice unchanged, when it could not be saved. Turned
// off, its pictures are let go at the next frame.
bool ShowSfvBackdrop(bool shown);

} // namespace sfve::adapter
