// GameContent's rule for which fighters a battle can load (BattleFighter),
// checked on a fighter table laid out as the game's (DA_CharaEntry records,
// SfvOffsets' sfv::content) without the game. The catalog's fighters and the
// pipe's launch both ask this one rule, so ELEVEN, whose entry names no
// battle setup, is neither offered nor launched.

#include <doctest/doctest.h>

#include "mod/GameContent.hxx"
#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"

#include <wchar.h>

#include <cstdint>
#include <vector>

using namespace sfve::mod;
namespace content = sfve::mod::sfv::content;

namespace {

// An FString as the game holds one: its characters with the terminator
// counted (an empty one holds just the terminator), or none at all (null).
GameString FString(const wchar_t* text)
{
	if (!text) return {nullptr, 0, 0};
	const int32_t count = (int32_t)wcslen(text) + 1;
	return {text, count, count};
}

// One DA_CharaEntry record's code and SetupData.
struct Entry {
	const wchar_t* code;
	const wchar_t* setup;
};

// A CharaAsset with one record per entry, in order.
struct FighterTable {
	explicit FighterTable(const std::vector<Entry>& entries) : records(entries.size() * content::kCharaStride, 0)
	{
		for (size_t i = 0; i < entries.size(); i++) {
			uint8_t* record = records.data() + i * content::kCharaStride;
			Field<GameString>(record, 0) = FString(entries[i].code);
			Field<int32_t>(record, content::kCharaUniqueId) = (int32_t)i + 1;
			Field<GameString>(record, content::kCharaSetupData) = FString(entries[i].setup);
		}
		Field<uint8_t*>(asset, content::kCharaEntries) = records.empty() ? nullptr : records.data();
		Field<int32_t>(asset, content::kCharaCount) = (int32_t)entries.size();
	}

	std::vector<uint8_t> records;
	alignas(8) uint8_t asset[64] = {};
};

} // namespace

TEST_CASE("a battle loads only a fighter whose entry in the game's fighter table names its own battle setup")
{
	// RYU and KEN name their setups. ELEVEN names none (the select screen
	// plays another fighter as Eleven), and Random's holds only the empty
	// FString's terminator.
	FighterTable table(std::vector<Entry>{
		{L"RYU", L"/Game/Chara/Z00/SetupData"}, {L"ELV", nullptr}, {L"RND", L""}, {L"KEN", L"/Game/Chara/Z01/SetupData"}});
	uint8_t* asset = table.asset;
	CHECK(BattleFighter(asset, L"RYU"));
	CHECK(BattleFighter(asset, L"KEN"));
	CHECK(BattleFighter(asset, L"ken")); // compared as the game compares its codes
	// `launch versus ELV RYU TRN` is refused by this rule, as the catalog
	// leaves ELEVEN out, rather than loading a battle that never starts.
	CHECK_FALSE(BattleFighter(asset, L"ELV"));
	CHECK_FALSE(BattleFighter(asset, content::kRandomFighter));
	// A code the table does not have, a part of one, a longer one, and none.
	CHECK_FALSE(BattleFighter(asset, L"ZZZ"));
	CHECK_FALSE(BattleFighter(asset, L"RY"));
	CHECK_FALSE(BattleFighter(asset, L"RYUU"));
	CHECK_FALSE(BattleFighter(asset, L""));
	CHECK_FALSE(BattleFighter(nullptr, L"RYU"));
	// A table with no entries yet has no fighter a battle can load.
	FighterTable empty(std::vector<Entry>{});
	CHECK_FALSE(BattleFighter(empty.asset, L"RYU"));
}
