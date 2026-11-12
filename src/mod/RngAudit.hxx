#pragma once

// Diagnostics (development, -sfve_rng_audit=1; docs/design/beta8/
// determinism.md): which random generators a battle draws from, against the
// rollback state.
//
// The battle's generators are objects {vtable rva sfv::rng::kVtable, uint32
// state at +8}; every draw through one goes through its virtual +24, the LCG
// step sfv::kRandomDraw (rva 0x32B2B0; the class's other draws, rva 0x32B280
// for a float and rva 0x32A830 for a coin, call it). A generator the layout
// does not hold would be neither reseeded at the session's start nor put back
// by a rollback, and two copies would draw apart from it. The layout holds
// each fighter's (the 24-byte pair, CharaUnit+0xC40) and the global one's
// state (sfv::rng::kGlobal), so none is expected.
//
// While the audit is on, every draw a bound battle's update makes is placed:
// in a region of the layout (and whether ReseedRandom reaches it: its vtable
// word in the same region), the global generator, or outside. A generator
// outside the layout, or in it but not reseeded, is logged once per battle
// with the return address of the draw's caller (an rva to read in IDA); each
// battle's end logs the counts. Nothing is changed: no draw is reseeded.
//
// What it cannot see: the LCG steps written inline on the global generator's
// state (rva 0x391C30 and the visual code's rva 0x3954F0..0x395EB0 step the
// dword at sfv::rng::kGlobal + 8 directly). That dword is the layout's
// "global.rng", so those are covered without a draw to audit.

#include "core/StateRegions.hxx"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

namespace rng_audit {

enum class Place { InLayout, Global, Outside };

// Where a generator lies against a layout. Pure.
class Ranges {
public:
	// `layout`'s regions; `global`: the global generator object's address.
	void Set(const StateLayout& layout, uintptr_t global);
	void Clear();
	bool Empty() const { return ranges_.empty(); }

	struct Found {
		Place place = Place::Outside;
		bool reseeded = false; // ReseedRandom reaches it: its vtable word, 8-aligned, in the same region
		size_t region = 0;     // the region holding its state (InLayout)
	};
	// The generator object at `stream`: its state's dword (sfv::rng::kState).
	Found Of(uintptr_t stream) const;
	const std::string& Name(size_t region) const { return names_.at(region); }

private:
	struct Range {
		uintptr_t begin = 0, end = 0;
		size_t region = 0;
	};
	std::vector<Range> ranges_; // by begin
	std::vector<std::string> names_;
	uintptr_t global_ = 0;
};

} // namespace rng_audit

// Hooks the draw (sfv::kRandomDraw); once, from SfveStart, only with
// -sfve_rng_audit=1. False (logged) when it could not be hooked.
bool InstallRngAudit(uintptr_t imageBase);
// SfvAdapter, game thread: the battle's layout as bound, each update (a
// bound battle's, `on`) as it starts and ends, and the battle's end, which
// logs the counts. Nothing when the audit is not installed.
void RngAuditBind(const StateLayout& layout, uintptr_t imageBase);
void RngAuditUpdate(bool on, bool fighting);
void RngAuditForget();

} // namespace sfve::mod
