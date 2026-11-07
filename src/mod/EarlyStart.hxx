#pragma once

// How the mod came to start (mod/EarlyStart.cxx): from the exe's entry point
// the launcher's injection hooked, or from the xinput1_3.dll proxy.

namespace sfve::mod {

// SfveStart was called from EarlyStart's entry hook (Launcher.exe's start).
bool StartedByEntryHook();
// The command line named the launcher and the entry point was hooked.
bool LauncherHookPlanted();

} // namespace sfve::mod
