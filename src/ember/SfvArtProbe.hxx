#pragma once

// The development probe for SFV's pictures (EMBER_LIBRARY.md section 43), on
// the control pipe. It answers what the pick screen's previews wait on: which
// of the game's UI assets load as whole textures, their sizes, what they
// show, and how the shop's costume pictures relate to the spec's costumes.
//
//   art load <path> [<path> ...]   queue each object path ("/Game/.../X.X")
//                                  in SfvArt's loader, drawn whole; the log's
//                                  "art: <path>: ..." lines say what came
//   art show [<path>]              draw that picture at its own size in the
//                                  top-left corner, over everything, for a
//                                  screenshot; with no path, stop
//   art data <CODE>                log the fighter's spec costumes (number,
//                                  CostumeDatas folder, name, and its
//                                  RefCostumeAvailableCode) and the shop's
//                                  rows whose thumbnail is shp_<CODE>_*
//
// Loading still runs only at the main menu, two a frame, through SfvArt's
// one path (RequestPicture). Development only: nothing a player needs, and
// RegisterArtProbe is the one call a build-time development gate wraps.

namespace sfve::adapter {

// Registers the `art` command. Once, after InstallSfvArt.
void RegisterArtProbe();

// Render thread, inside ImGui's frame: the picture `art show` named, if any.
void DrawArtProbe();

} // namespace sfve::adapter
