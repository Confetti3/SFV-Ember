#pragma once

// SFV grants every player everything in the game itself: every fighter,
// costume, colour and stage this copy has installed, in its own menus,
// Story, Arcade, Training and versus as much as in Ember's rooms
// (EMBER_LIBRARY.md section 57; the maintainer, 2026-11-10). Ownership,
// when it applies, is Ember's gate alone (kOwnershipGateByDefault, off while
// Capcom's entitlement server is down), and that gate judges by the
// account's own answer, which is kept apart here.
//
// Where: the spec's code cache (sfv::codecache), which the spec's refresh
// (step 8 of its queue, rva 0x57CE10) asks instead of the account. Its grant
// pass (sfv::kSpecGrantPass) writes the account's answer for each code; the
// hook lets it, records that answer (AccountGrants), and then marks every
// code of the four kinds the refreshes read granted, so the fighters',
// costumes', colours' and stages' refreshes that follow set every spec flag
// from it, and every reader of those flags (the game's listers, its battle
// jump's check, Story's re-pick) agrees. The cache lives in DA_Spec's
// object, not in a save: the account's entitlements (UKWEntitlementsData,
// what the save keeps) are only read, never written, and the game's local
// unlocks are still recorded, since they ask the account directly
// (rva 0x7A3130).
//
// The account's answer stays readable for Ember: GameContent's `entitled`
// is made from AccountGrantsNow(), not from the spec's flags the game now
// sees.

#include <stdint.h>

#include <array>
#include <string>
#include <vector>

namespace sfve::mod {

// What the account's entitlements granted at the spec's last refresh: the
// keys of the granted codes of each kind the refreshes read
// (sfv::codecache::kFighterCodes and the rest), as the game's own reader
// gathers them (rva 0x54F680: an entry with a row, marked granted).
struct AccountGrants {
	// A refresh ran through the hook and its cache read as Ember knows it.
	// Without that, the spec's flags are still the account's: the hook
	// writes nothing it has not read first.
	bool captured = false;
	std::array<std::vector<uint64_t>, 4> keys; // each sorted

	// The account grants the code `key` of `kind` (0-3); false for any other
	// kind.
	bool Grants(int32_t kind, uint64_t key) const;
};

// One pass over the spec's code cache (ReadAndGrantCodeCache).
struct GrantPass {
	AccountGrants account;
	std::array<int, 4> codes{};          // entries with a row, by kind
	std::array<int, 4> accountGranted{}; // of those, the account's
	int marked = 0;                      // entries the pass marked granted
	// The cache is not laid out as Ember knows (a chain past the pairs, a
	// bucket count that is not a power of two, an entry count below zero):
	// nothing was written, and `account` is not captured.
	std::string error;
};

// Reads the spec's code cache (the object `spec`, laid out as
// sfv::codecache says) as the game's reader walks it, recording which codes
// of kinds 0-3 the account grants, and then, with `grantAll`, marks every
// entry of those kinds that has a row granted. Never clears a mark, never
// touches an entry without a row or a kind past 3, and writes nothing when
// any kind cannot be read. No game code: the tests call it on memory laid
// out as the game's.
GrantPass ReadAndGrantCodeCache(uint8_t* spec, bool grantAll);

// Takes the marks back before the game's own pass: every entry of kinds 0-3
// with a row unmarked, which that pass then marks from the account's answer
// (rva 0x543EB0 writes every such entry). So the answer read after it is the
// account's even should the pass leave the cache alone (it does when the
// account's entitlements are missing: the refreshes then set nothing
// either). Empty, or why the cache could not be read, in which case nothing
// was written. No game code, as ReadAndGrantCodeCache.
std::string ClearCodeCacheMarks(uint8_t* spec);

// Hooks the spec's grant pass (a set of one) so every refresh grants
// everything when `grantAll`, and records the account's answer either way;
// logs one line at the main menu with the game's counts and the account's.
// From SfveStart, before the game's code runs (the refresh runs once, at
// boot). False, logged, when the hook could not go in: the game then grants
// what the account's cache holds, as it always did.
bool InstallEntitlementGrant(uintptr_t imageBase, bool grantAll);

// Game thread: the account's answer from the spec's last refresh; not
// captured before one ran through the hook.
AccountGrants AccountGrantsNow();

} // namespace sfve::mod
