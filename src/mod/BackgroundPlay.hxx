#pragma once

// Keeps a copy of the game playing while another window has the focus.
//
// SFV reads its pads (and keeps its battle running) only while it believes it
// is the foreground application, which it decides by asking Windows which
// window is in front. With background play on, that question answers with
// this process's own window, so two copies side by side, or a copy behind a
// stream tool, both keep running and reading their (virtual) pads.
//
// Off unless asked for at launch (-sfve_background) or by the player's saved
// Play in the background (dllmain, as the game starts); it changes nothing
// else.

namespace sfve::mod {

bool InstallBackgroundPlay();

} // namespace sfve::mod
