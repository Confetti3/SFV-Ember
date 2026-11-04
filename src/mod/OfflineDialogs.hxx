#pragma once

// The mod keeps SFV offline (NetGuard refuses every Capcom host), so at boot
// the game's online start-up fails and it stops the player twice:
// "Failed to retrieve data. [80003]" (the EULA download) and the notice that
// play continues offline. Neither tells the player anything they can act on.
//
// The game raises network errors as codes on a queue; its tick broadcasts
// each code to listeners. One listener (the error-dialog service, hooked here)
// turns a code into a dialog; another (the boot state machine) moves boot on
// to offline mode. The boot flow then waits only while the service counts an
// open dialog, so a dialog that is never opened lets it continue at once, as
// if OK had been pressed. Only the download failures that our blocking
// causes are dropped; every other error still shows. The offline notice is
// posted fire-and-forget by name after boot has already moved on.

#include <stdint.h>

namespace sfve::mod {

enum class OfflineDialogs {
	Show,              // every dialog shows
	HideDownloadFails, // the blocked downloads' errors do not show
	HideAll,           // nor does the offline-mode notice
};

bool InstallOfflineDialogs(uintptr_t imageBase, OfflineDialogs level);

// How many dialogs were kept from showing.
uint32_t SuppressedDialogs();

} // namespace sfve::mod
