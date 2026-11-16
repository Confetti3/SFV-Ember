#pragma once

// What each copy's battle computes with, which the copies agree on before
// frame 0 (core/StartAgreement.hxx; docs/design/beta10/desync.md): per
// fighter unit, in the battle's order, the fighter, the balance version its
// moves were loaded at, and a fingerprint of the move data the battle bound.
//
// Two copies that load a fighter at another balance version, or with other
// move data (an older or altered game file, a mod), play the same inputs to
// different states. Nothing in the start state shows it: the state is equal
// at frame 0 and parts only when a move whose data differs is used, which a
// checkpoint then reports as "the two games diverged" (the 2026-11-15 field
// desyncs: the same script and frame on both copies, the action controller's
// bytes apart). Compared here, a difference ends the match before frame 0
// and is named as what it is.
//
// Pure: no game, no clock, no network.

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

namespace sfve::match_data {

// A version a copy could not read.
constexpr int32_t kUnknownVersion = -1;
// A fighter code's most characters on the wire (the game's are three).
constexpr size_t kCodeBytes = 8;
// More fighter units than any battle binds (SFV: two, four with Akira's partners).
constexpr size_t kMostFighters = 8;

struct Fighter {
	std::string code;                  // the game's code ("Z41"); empty when not read
	int8_t side = -1;                  // the side the unit fights for: 0 P1, 1 P2; -1 not read
	int32_t version = kUnknownVersion; // the balance version the battle loaded
	// A hash of the move data the battle bound for this unit; known false
	// when this copy could not read it (then it is not compared).
	bool dataKnown = false;
	uint64_t data = 0;
	bool operator==(const Fighter&) const = default;
};

struct Facts {
	std::vector<Fighter> fighters; // the battle's fighter units, in its order
	bool operator==(const Facts&) const = default;
};

// The first way two copies' facts part, by unit.
struct Difference {
	enum class What : uint8_t {
		Units,   // another number of fighter units
		Fighter, // another fighter in a unit, or the unit on another side
		Version, // another balance version
		Data,    // the same version, other move data
	};
	What what = What::Units;
	size_t unit = 0;      // the unit's place; for Units, the shorter list's length
	Fighter local;        // this copy's (empty for Units past its list)
	Fighter reference;    // the other copy's
	bool operator==(const Difference&) const = default;
};

// Where `local` and `reference` part, or none when they agree. A version
// either side could not read, or data either side could not hash, is not
// compared: only a difference both copies know is one.
std::optional<Difference> Compare(const Facts& local, const Facts& reference);

// One line for the log and the report: "game data differs (fighter Z41):
// balance version 61 here, 43 at the other copy".
std::string Describe(const Difference& d);

// The players' notice: what differs, and what to do about it.
std::string Notice(const Difference& d);

// The facts in one line, for the log and the report: "Z41 v61 data 1a2b...,
// Z25 v60 data ...".
std::string Text(const Facts& facts);

// The version of the first unit fighting for `side` (a player's own
// fighter: partners come after the two fighters), or kUnknownVersion.
int32_t SideVersion(const Facts& facts, int side);

// The wire form (little-endian): u8 count, then per fighter the code in
// kCodeBytes NUL-padded bytes, i32 version, u8 data known, i8 side, 2 pad
// bytes, u64 data. EncodedSize is its length; Decode refuses a count past
// kMostFighters, a size that does not fit it, or a code that is not
// printable ASCII.
size_t EncodedSize(const Facts& facts);
void Encode(const Facts& facts, std::vector<uint8_t>* out);
bool Decode(const uint8_t* data, size_t size, Facts* out);

} // namespace sfve::match_data
