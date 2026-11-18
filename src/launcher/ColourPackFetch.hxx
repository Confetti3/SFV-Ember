#pragma once

// The launcher fetches the colour pack the package names
// (docs/design/COLOUR_PACK.md): every colour of every costume but the first,
// which the zip does not carry. The game cannot: its network guard keeps it
// off every host but loopback and its peers, and stays as it is. The launcher
// runs outside it.
//
// One thread, started as the launcher prepares the first start, so the
// download overlaps the game's own start-up. It reads the descriptor beside
// Launcher.exe (colours::PackagePack; none: nothing to do), leaves only that
// pack in the player's data folder (PruneOtherPacks), and when the pack is
// not there downloads it with the updater's HTTP download (redirects
// followed, stopped past the descriptor's size) and installs it
// (InstallPack: size, SHA-256, the updater's zip reader, one rename). A log
// line at each quarter of the download and for the outcome; no window (the
// launcher shows none while the game runs). Anything that fails, offline
// included, is a log line and is tried again at the next start: the game
// never waits for the pack, and shows each costume's first colour until it is
// there. Closing the launcher cancels a fetch under way.

namespace sfve::launcher {

// The fetch's thread, started once; a later call does nothing.
void StartColourPackFetch();

} // namespace sfve::launcher
