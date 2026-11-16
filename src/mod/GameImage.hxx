#pragma once

// The game's executable image: where it lies, and its functions by their
// place in it (CodeSite, checked against the build by BuildCheck). The one
// way the mod finds a game function it calls by address. The exe's base is
// the same for the life of the process (GetModuleHandleW(nullptr)).

#include "mod/BuildCheck.hxx"

#include <stdint.h>

#include <windows.h>

namespace sfve::mod {

inline uintptr_t GameBase()
{
	return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

// The game function at `site`, as `Fn`.
template <typename Fn>
Fn GameFn(const CodeSite& site)
{
	return reinterpret_cast<Fn>(GameBase() + site.rva);
}

} // namespace sfve::mod
