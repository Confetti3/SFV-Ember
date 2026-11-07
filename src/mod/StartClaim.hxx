#pragma once

// One start of the mod per process. Two copies of sfv_ember.dll can load into
// one game: the one Launcher.exe injects from the package, and the one the
// xinput1_3.dll proxy loads from the game folder when it is still deployed
// there. They are separate modules, each with its own statics, so the first
// to start claims the process in a named section (Local\SFVEmber.Mod.<pid>)
// with its path and how it was started; a later one reads that and stays
// inert, without installing a hook.

#include <windows.h>

#include <string>

namespace sfve::mod {

struct StartClaimResult {
	bool won = false;
	std::wstring module;        // this copy's path
	std::wstring winnerModule;  // the copy that started first, when this one lost
	std::string winnerLoader;
	DWORD error = 0;            // a section that could not be made or mapped
};

// loader: how this copy was started ("launcher", "proxy").
StartClaimResult ClaimStart(const char* loader);

} // namespace sfve::mod
