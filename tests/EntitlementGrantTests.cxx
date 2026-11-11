// SFV grants every player everything (EntitlementGrant, EMBER_LIBRARY.md
// section 57), checked without the game: the grant pass over a code cache
// laid out as the game's (sfv::codecache) records the account's own answer
// and marks every code the refreshes read granted, and writes nothing to a
// cache it cannot read; and the lists Ember's gate judges by
// (LimitToAccount) stay the account's while the game grants everything.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "mod/EntitlementGrant.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"

#include <cstdint>
#include <optional>
#include <vector>

using namespace sfve;
using namespace sfve::catalog_fixture;
namespace cc = sfve::mod::sfv::codecache;
using mod::Field;

namespace {

// One code of the cache: its KWEntitlementSpec row (null for none), whether
// the account grants it, and its key.
struct Code {
	bool row;
	bool granted;
	uint64_t key;
};

// A pair of the cache's map: its kind and its codes.
struct Kind {
	int32_t kind;
	std::vector<Code> codes;
};

// A SpecData whose code cache holds `kinds` in that order of pairs, hashed
// into `bucketCount` buckets (0: an empty map; 1: the inline bucket, no
// allocation), each bucket's chain in the pairs' order.
struct FakeSpec {
	FakeSpec(const std::vector<Kind>& kinds, int32_t bucketCount) : pairs(kinds.size() * cc::kPairStride, 0)
	{
		buckets.assign(bucketCount > 1 ? (size_t)bucketCount : 1, -1);
		entries.resize(kinds.size());
		for (size_t i = 0; i < kinds.size(); i++) {
			std::vector<uint8_t>& data = entries[i];
			data.assign(kinds[i].codes.size() * cc::kEntryStride, 0);
			for (size_t c = 0; c < kinds[i].codes.size(); c++) {
				uint8_t* entry = data.data() + c * cc::kEntryStride;
				Field<void*>(entry, cc::kEntryRow) = kinds[i].codes[c].row ? &row : nullptr;
				Field<uint8_t>(entry, cc::kEntryGranted) = kinds[i].codes[c].granted ? 1 : 0;
				Field<uint64_t>(entry, cc::kEntryKey) = kinds[i].codes[c].key;
			}
			uint8_t* pair = Pair(i);
			Field<int32_t>(pair, cc::kPairKind) = kinds[i].kind;
			Field<uint8_t*>(pair, cc::kPairEntries) = data.empty() ? nullptr : data.data();
			Field<int32_t>(pair, cc::kPairEntryCount) = (int32_t)kinds[i].codes.size();
			Field<int32_t>(pair, cc::kPairNext) = -1;
			if (bucketCount <= 0) continue;
			// Appended to its bucket's chain.
			int32_t* link = &buckets[(size_t)(kinds[i].kind & (bucketCount - 1))];
			while (*link != -1) link = &Field<int32_t>(Pair((size_t)*link), cc::kPairNext);
			*link = (int32_t)i;
		}
		Field<uint8_t*>(spec, cc::kMap) = pairs.empty() ? nullptr : pairs.data();
		Field<int32_t>(spec, cc::kMapCount) = (int32_t)kinds.size();
		Field<int32_t>(spec, cc::kMapBucketCount) = bucketCount;
		if (bucketCount == 1) {
			Field<int32_t>(spec, cc::kMapInlineBucket) = buckets[0];
		} else if (bucketCount > 1) {
			Field<int32_t*>(spec, cc::kMapBuckets) = buckets.data();
		}
	}

	uint8_t* Pair(size_t i) { return pairs.data() + i * cc::kPairStride; }
	uint8_t Granted(size_t pair, size_t code) const { return entries[pair][code * cc::kEntryStride + cc::kEntryGranted]; }
	std::vector<std::vector<uint8_t>> Snapshot() const { return entries; }

	alignas(8) uint8_t spec[512] = {};
	std::vector<uint8_t> pairs;
	std::vector<int32_t> buckets;
	std::vector<std::vector<uint8_t>> entries;
	int row = 0; // what a row pointer points at
};

// Fighters' codes (two granted, one not, one without a row), costumes' (none
// granted), colours' (one, granted), stages' (one granted, one not), and a
// fifth kind no refresh reads. Kinds 0 and 4 share a bucket of four, so the
// walk follows a chain.
std::vector<Kind> Cache()
{
	return {{4, {{true, false, 50}}}, {0, {{true, true, 10}, {true, false, 11}, {false, false, 12}, {true, true, 13}}},
		{1, {{true, false, 20}}}, {2, {{true, true, 30}}}, {3, {{true, true, 40}, {true, false, 41}}}};
}

// Every `owned` of the lists, in order: fighters, their costumes and
// colours, then the stages.
std::vector<bool> OwnedFlags(const mod::ContentLists& lists)
{
	std::vector<bool> flags;
	for (const mod::FighterContent& fighter : lists.fighters) {
		flags.push_back(fighter.owned);
		for (const mod::CostumeContent& costume : fighter.costumes) {
			flags.push_back(costume.owned);
			for (const mod::ColourContent& colour : costume.colours) flags.push_back(colour.owned);
		}
	}
	for (const mod::StageContent& stage : lists.stages) flags.push_back(stage.owned);
	return flags;
}

// The lists as the game's answer gives them while it grants everything:
// every fighter, costume and colour owned (the game's lists offer all), the
// game's flags all set, the account's own `entitled` and `visible` as they
// were.
mod::ContentLists GrantedByTheGame(mod::ContentLists lists)
{
	for (mod::FighterContent& fighter : lists.fighters) {
		fighter.owned = fighter.granted = true;
		for (mod::CostumeContent& costume : fighter.costumes) {
			costume.owned = costume.granted = true;
			for (mod::ColourContent& colour : costume.colours) colour.owned = colour.granted = true;
		}
	}
	for (mod::StageContent& stage : lists.stages) stage.granted = stage.installed;
	return lists;
}

} // namespace

TEST_CASE("the grant pass records the account's answer, then marks every code the refreshes read granted")
{
	FakeSpec cache(Cache(), 4);
	const auto before = cache.Snapshot();

	// Granting off: the account's answer is recorded and nothing is written.
	const mod::GrantPass off = mod::ReadAndGrantCodeCache(cache.spec, false);
	CHECK(off.error.empty());
	REQUIRE(off.account.captured);
	CHECK(cache.Snapshot() == before);
	CHECK(off.codes == std::array<int, 4>{3, 1, 1, 2}); // the code without a row is not one
	CHECK(off.accountGranted == std::array<int, 4>{2, 0, 1, 1});
	CHECK(off.marked == 0);
	CHECK(off.account.keys[0] == std::vector<uint64_t>{10, 13});
	CHECK(off.account.keys[1].empty());
	CHECK(off.account.keys[2] == std::vector<uint64_t>{30});
	CHECK(off.account.keys[3] == std::vector<uint64_t>{40});

	// Granting: the same answer, read before the marks.
	const mod::GrantPass on = mod::ReadAndGrantCodeCache(cache.spec, true);
	REQUIRE(on.account.captured);
	CHECK(on.account.keys == off.account.keys);
	CHECK(on.marked == 3);
	CHECK(cache.Granted(1, 0) == 1); // granted before, still
	CHECK(cache.Granted(1, 1) == 1); // marked
	CHECK(cache.Granted(1, 2) == 0); // no row: the refreshes never take it, and it is left alone
	CHECK(cache.Granted(1, 3) == 1);
	CHECK(cache.Granted(2, 0) == 1);
	CHECK(cache.Granted(3, 0) == 1);
	CHECK(cache.Granted(4, 1) == 1);
	CHECK(cache.Granted(0, 0) == 0); // kind 4: no refresh reads it
	// The account's answer by kind and key.
	CHECK(on.account.Grants(0, 13));
	CHECK_FALSE(on.account.Grants(0, 11));
	CHECK_FALSE(on.account.Grants(1, 20));
	CHECK(on.account.Grants(3, 40));
	CHECK_FALSE(on.account.Grants(3, 41));
	CHECK_FALSE(on.account.Grants(4, 50));
	CHECK_FALSE(on.account.Grants(-1, 10));

	// A later refresh: the hook first takes its marks back (every code of the
	// four kinds with a row unmarked; the game's own pass then writes the
	// account's answer into each), so what it reads after is never an earlier
	// grant.
	CHECK(mod::ClearCodeCacheMarks(cache.spec).empty());
	CHECK(cache.Granted(1, 0) == 0);
	CHECK(cache.Granted(1, 1) == 0);
	CHECK(cache.Granted(4, 0) == 0);
	CHECK(cache.Granted(0, 0) == 0); // kind 4 untouched (never marked)
	// The game's pass answering as before, then the hook's read.
	FakeSpec answered(Cache(), 4);
	const mod::GrantPass again = mod::ReadAndGrantCodeCache(answered.spec, true);
	CHECK(again.account.keys == off.account.keys);
}

TEST_CASE("taking the marks back leaves the kinds no refresh reads, and a cache it cannot read, as they are")
{
	FakeSpec cache({{4, {{true, true, 50}}}, {0, {{true, true, 10}, {false, true, 12}}}}, 4);
	CHECK(mod::ClearCodeCacheMarks(cache.spec).empty());
	CHECK(cache.Granted(1, 0) == 0);
	CHECK(cache.Granted(1, 1) == 1); // no row: not the refreshes', not the hook's
	CHECK(cache.Granted(0, 0) == 1); // kind 4
	FakeSpec unknown(Cache(), 4);
	const auto before = unknown.Snapshot();
	Field<int32_t>(unknown.spec, cc::kMapBucketCount) = 3;
	CHECK_FALSE(mod::ClearCodeCacheMarks(unknown.spec).empty());
	CHECK(unknown.Snapshot() == before);
}

TEST_CASE("the grant pass finds kinds through the inline bucket, and an empty map has no codes")
{
	FakeSpec inlineBucket(Cache(), 1);
	const mod::GrantPass pass = mod::ReadAndGrantCodeCache(inlineBucket.spec, true);
	CHECK(pass.error.empty());
	CHECK(pass.account.captured);
	CHECK(pass.codes == std::array<int, 4>{3, 1, 1, 2});
	CHECK(pass.marked == 3);

	FakeSpec empty({}, 0);
	const mod::GrantPass none = mod::ReadAndGrantCodeCache(empty.spec, true);
	CHECK(none.error.empty());
	CHECK(none.account.captured);
	CHECK(none.codes == std::array<int, 4>{0, 0, 0, 0});
	CHECK(none.marked == 0);

	// A kind with no pair has no codes; the others are still read.
	FakeSpec noStages({{0, {{true, false, 10}}}}, 2);
	const mod::GrantPass partial = mod::ReadAndGrantCodeCache(noStages.spec, true);
	CHECK(partial.error.empty());
	CHECK(partial.codes == std::array<int, 4>{1, 0, 0, 0});
	CHECK(noStages.Granted(0, 0) == 1);
}

TEST_CASE("a code cache laid out otherwise than Ember knows is left as it is")
{
	// A bucket count that is not a power of two.
	{
		FakeSpec cache(Cache(), 4);
		const auto before = cache.Snapshot();
		Field<int32_t>(cache.spec, cc::kMapBucketCount) = 3;
		const mod::GrantPass pass = mod::ReadAndGrantCodeCache(cache.spec, true);
		CHECK_FALSE(pass.error.empty());
		CHECK_FALSE(pass.account.captured);
		CHECK(pass.marked == 0);
		CHECK(cache.Snapshot() == before);
	}
	// More than one bucket and no allocation for them.
	{
		FakeSpec cache(Cache(), 4);
		const auto before = cache.Snapshot();
		Field<int32_t*>(cache.spec, cc::kMapBuckets) = nullptr;
		const mod::GrantPass pass = mod::ReadAndGrantCodeCache(cache.spec, true);
		CHECK_FALSE(pass.error.empty());
		CHECK(cache.Snapshot() == before);
	}
	// A chain that loops: bounded by the pairs' count.
	{
		FakeSpec cache(Cache(), 4);
		const auto before = cache.Snapshot();
		Field<int32_t>(cache.Pair(1), cc::kPairNext) = 0; // kind 0's pair back to kind 4's, which leads to it
		Field<int32_t>(cache.Pair(1), cc::kPairKind) = 8; // and kind 0 is no longer there to stop the walk
		const mod::GrantPass pass = mod::ReadAndGrantCodeCache(cache.spec, true);
		CHECK_FALSE(pass.error.empty());
		CHECK(cache.Snapshot() == before);
	}
	// A chain that leaves the pairs.
	{
		FakeSpec cache(Cache(), 4);
		const auto before = cache.Snapshot();
		Field<int32_t>(cache.Pair(0), cc::kPairNext) = 9;
		Field<int32_t>(cache.Pair(1), cc::kPairKind) = 8;
		const mod::GrantPass pass = mod::ReadAndGrantCodeCache(cache.spec, true);
		CHECK_FALSE(pass.error.empty());
		CHECK(cache.Snapshot() == before);
	}
	// A kind whose count is below zero: nothing of any kind is written, the
	// kinds read before it included.
	{
		FakeSpec cache(Cache(), 4);
		const auto before = cache.Snapshot();
		Field<int32_t>(cache.Pair(4), cc::kPairEntryCount) = -1;
		const mod::GrantPass pass = mod::ReadAndGrantCodeCache(cache.spec, true);
		CHECK_FALSE(pass.error.empty());
		CHECK_FALSE(pass.account.captured);
		CHECK(cache.Granted(1, 1) == 0);
		CHECK(cache.Snapshot() == before);
	}
}

TEST_CASE("while the game grants everything, Ember's lists stay the account's, and its gate still refuses what the account lacks")
{
	// The lists the catalog takes from a copy whose game grants nothing
	// beyond the account's cache, and from the same copy while the game
	// grants everything: the same `owned` everywhere.
	mod::ContentLists account = TestContent();
	mod::LimitToAccount(account);
	mod::ApplyContentOwnership(account, mod::ContentOwnership::Entitlements);
	mod::ContentLists granted = GrantedByTheGame(TestContent());
	mod::LimitToAccount(granted);
	mod::ApplyContentOwnership(granted, mod::ContentOwnership::Entitlements);
	REQUIRE(OwnedFlags(granted) == OwnedFlags(account));
	mod::ContentLists unlimited = TestContent();
	mod::ApplyContentOwnership(unlimited, mod::ContentOwnership::Entitlements);
	CHECK(OwnedFlags(account) == OwnedFlags(unlimited)); // the account's cache already decided TestContent's
	// Not the game's: it offered all of them.
	CHECK_FALSE(granted.fighters[2].owned);            // KEN
	CHECK_FALSE(granted.fighters[1].costumes[2].owned); // CNL's costume 3, which the cache lacks
	CHECK_FALSE(granted.fighters[1].costumes[2].colours[0].owned);
	CHECK_FALSE(granted.fighters[0].costumes[2].colours[3].owned); // RYU's EX02 of costume 4
	CHECK(granted.fighters[1].costumes[2].granted);
	CHECK_FALSE(granted.stages[1].owned); // S02: installed, its codes not in the account's cache

	// So Ember's gate, on, refuses them as it did before the grant: the test
	// catalog (filled from TestContent, gate on) holds exactly these `owned`.
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	const ember::game::GameState& state = ember::game::Game().state;
	CHECK_FALSE(state.FighterAvailability(2).ready);
	CHECK_FALSE(catalog.availablePick(PickOf(2), false, state.FighterAvailability(2), nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(1, 3, 0), false, state.FighterAvailability(1), nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(0, 4, 101), false, state.FighterAvailability(0), nullptr));
	CHECK(catalog.availablePick(PickOf(1, 0, 1), false, state.FighterAvailability(1), nullptr));
	// And the opponent's pick plays as picked: a room's match launches CNL's
	// costume 3, as it did.
	adapter::RoomMatch match = MatchOf(0, 1, 0);
	match.picks[1] = PickOf(1, 3, 0);
	const std::optional<mod::MatchPicks> picks = adapter::LaunchPicks(match).picks;
	REQUIRE(picks);
	CHECK(picks->players[1].costume == 3);
}

TEST_CASE("the account's limits follow the refresh's rules")
{
	// One fighter, every flag of the game's answer set.
	const auto lists = [](bool visible, bool installed, bool costumeEntitled, bool colourEntitled) {
		mod::ContentLists made;
		mod::FighterContent fighter = Fighter("RYU", {Costume(0, {0, 1}, "", installed)});
		fighter.visible = visible;
		fighter.costumes[0].entitled = costumeEntitled;
		fighter.costumes[0].colours[0].entitled = colourEntitled;
		fighter.costumes[0].colours[1].entitled = false;
		made.fighters.push_back(fighter);
		return made;
	};
	mod::ContentLists all = lists(true, true, true, true);
	mod::LimitToAccount(all);
	CHECK(all.fighters[0].owned);
	CHECK(all.fighters[0].costumes[0].owned);
	CHECK(all.fighters[0].costumes[0].colours[0].owned);
	CHECK_FALSE(all.fighters[0].costumes[0].colours[1].owned);
	// The fighter's RefVisibleCode not granted: no fighter list keeps it.
	mod::ContentLists hidden = lists(false, true, true, true);
	mod::LimitToAccount(hidden);
	CHECK_FALSE(hidden.fighters[0].owned);
	CHECK(hidden.fighters[0].costumes[0].owned);
	// No costume of it selectable (not installed; or no colour granted): no
	// fighter list keeps it either.
	mod::ContentLists uninstalled = lists(true, false, true, true);
	mod::LimitToAccount(uninstalled);
	CHECK_FALSE(uninstalled.fighters[0].owned);
	mod::ContentLists colourless = lists(true, true, true, false);
	mod::LimitToAccount(colourless);
	CHECK_FALSE(colourless.fighters[0].owned);
	CHECK(colourless.fighters[0].costumes[0].owned); // the costume list asks only the costume's code
	CHECK_FALSE(colourless.fighters[0].costumes[0].colours[0].owned);
	// While the battle setup asset restricts the picks, its own colour list
	// decides the colours; the costumes and fighters still need the cache.
	mod::ContentLists restricted = lists(true, true, false, true);
	restricted.ownership.restricted = true;
	mod::LimitToAccount(restricted);
	CHECK(restricted.fighters[0].costumes[0].colours[1].owned);
	CHECK_FALSE(restricted.fighters[0].costumes[0].owned);
	CHECK_FALSE(restricted.fighters[0].owned);
	// Never the other way.
	mod::ContentLists notOffered = lists(true, true, true, true);
	notOffered.fighters[0].owned = false;
	notOffered.fighters[0].costumes[0].owned = false;
	mod::LimitToAccount(notOffered);
	CHECK_FALSE(notOffered.fighters[0].owned);
	CHECK_FALSE(notOffered.fighters[0].costumes[0].owned);
}
