#include "ember/SfvCatalog.hxx"

#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"
#include "common/RoomRules.hxx"

#include <xxhash.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

// Written once by FillCatalog, before the runtime's threads read the catalog.
mod::ContentLists s_lists;
std::string s_buildId = kMatchBuild;
// The runtime has started (SealCatalog): nothing writes the catalog again.
bool s_sealed = false;
// The roster in the order of the game's list, which is its ids' order.
std::vector<int> s_rosterOrder;
// The table rules a room offers that SFV plays (VersusLaunch's
// PlayableRounds and RoundClockFrames), in the room's order.
std::vector<std::uint8_t> s_roundCounts;
std::vector<std::uint16_t> s_roundTimes;

const mod::FighterContent* FighterAt(std::int64_t fighter)
{
	return fighter >= 0 && fighter < (std::int64_t)s_lists.fighters.size() ? &s_lists.fighters[(size_t)fighter] : nullptr;
}

const mod::CostumeContent* CostumeAt(std::int64_t fighter, int costume)
{
	const mod::FighterContent* content = FighterAt(fighter);
	if (!content) return nullptr;
	for (const mod::CostumeContent& entry : content->costumes) {
		if (entry.id == costume) return &entry;
	}
	return nullptr;
}

bool HasColour(const mod::CostumeContent& costume, int colour)
{
	return std::find(costume.colours.begin(), costume.colours.end(), colour) != costume.colours.end();
}

// A costume of the fighter the spec has, in one of that costume's colours.
bool ValidPick(const sf4e::selection::Pick& pick, bool)
{
	const mod::CostumeContent* costume = CostumeAt(pick.fighter, pick.costume);
	return costume && HasColour(*costume, pick.color) && pick.ultra == 0 && pick.handicap == 0;
}

bool ValidStage(std::int64_t id)
{
	return id >= 0 && id < (std::int64_t)s_lists.stages.size();
}

// This copy's install: a fighter whose package it lacks cannot be played
// here, whatever the spec says (GameContent's `installed`).
bool FighterLocked(int fighter, const sf4e::selection::Availability&)
{
	const mod::FighterContent* content = FighterAt(fighter);
	return !content || !content->installed;
}

// A valid pick this copy can load. Ownership is not judged here: the game's
// own check replaces unowned looks as the battle launches (VersusLaunch's
// checkOwnership), as SfvGame's FighterAvailability reports everything owned.
bool AvailablePick(const sf4e::selection::Pick& pick, bool editionSelect, const sf4e::selection::Availability& availability,
	const sf4e::selection::InstalledContent*)
{
	const mod::CostumeContent* costume = CostumeAt(pick.fighter, pick.costume);
	return ValidPick(pick, editionSelect) && !FighterLocked(pick.fighter, availability) && costume->installed;
}

// Each part SFV refuses becomes the game's first of its kind, in this order
// so that each is fitted to the one before it: fighter 0; the fighter's first
// costume; that costume's first colour; no Ultra and no handicap. Only the
// spec decides, so every copy fits a pick the same way.
bool NormalizePick(sf4e::selection::Pick& pick, bool, const sf4e::selection::Availability*,
	const sf4e::selection::InstalledContent*)
{
	const sf4e::selection::Pick before = pick;
	if (!FighterAt(pick.fighter)) pick.fighter = 0;
	const mod::FighterContent* fighter = FighterAt(pick.fighter);
	const mod::CostumeContent* costume = CostumeAt(pick.fighter, pick.costume);
	if (!costume && fighter) {
		costume = &fighter->costumes.front();
		pick.costume = costume->id;
	}
	if (costume && !HasColour(*costume, pick.color)) pick.color = costume->colours.front();
	pick.ultra = 0;
	pick.handicap = 0;
	return pick.fighter != before.fighter || pick.costume != before.costume || pick.color != before.color ||
		pick.ultra != before.ultra || pick.handicap != before.handicap;
}

// SFV has no Random stage: a choice is a stage of the list, else the first.
int ResolveStage(int choice, std::uint32_t, const sf4e::selection::RandomPool&, const sf4e::selection::InstalledContent*)
{
	return ValidStage(choice) ? choice : 0;
}

// The game's name for the fighter, or its code where it has none. The shell
// asks for fighter 0 even when the catalog is empty (its lists were not
// filled or were refused), so an id outside the roster reads "?".
const char* FighterName(int fighter)
{
	const mod::FighterContent* content = FighterAt(fighter);
	if (!content) return "?";
	return content->name.empty() ? content->code.c_str() : content->name.c_str();
}

// The shared code's "numbered from 0" does not hold for SFV: a fighter's
// costume numbers are places in its CostumeDatas, and the spec may leave
// some out. One past the highest is what the one reader (a replay's "look
// unknown") needs: every costume the spec has reads as known.
int CostumeCount(int fighter)
{
	int count = 0;
	if (const mod::FighterContent* content = FighterAt(fighter)) {
		for (const mod::CostumeContent& costume : content->costumes) count = std::max(count, costume.id + 1);
	}
	return count;
}

// Empty until FillCatalog. SFV offers no public rooms yet, so the showcase
// faces are simply the first four fighters of the roster.
constinit ember::GameCatalog s_catalog = {0, 0, 0, 1, &ValidPick, &ValidStage, {0, 1, 2, 3}, -1, &FighterLocked,
	&AvailablePick, &NormalizePick, &ResolveStage, &FighterName, &CostumeCount, nullptr, nullptr, 0, nullptr, 0, false,
	true};

// The room's round counts and times SFV plays, so a table offers no other.
void FillRules()
{
	for (std::uint8_t rounds : sf4e::room::RoundCounts) {
		if (mod::PlayableRounds(rounds)) s_roundCounts.push_back(rounds);
	}
	for (std::uint16_t seconds : sf4e::room::RoundTimes) {
		if (mod::RoundClockFrames(seconds)) s_roundTimes.push_back(seconds);
	}
	s_catalog.roundCounts = s_roundCounts.data();
	s_catalog.roundCountChoices = (int)s_roundCounts.size();
	s_catalog.roundTimes = s_roundTimes.data();
	s_catalog.roundTimeChoices = (int)s_roundTimes.size();
}

// Why the lists cannot make a pick space, or empty when they can: every
// fighter has a costume and every costume a colour (NormalizePick's first of
// each), and every number fits the wire's byte (SelectionPick's ToNative).
std::string PickSpaceProblem(const mod::ContentLists& lists)
{
	if (lists.fighters.empty() || lists.stages.empty()) return "a list is empty";
	if (lists.fighters.size() > (size_t)ember::MaxFighters) return "more fighters than a room carries";
	for (const mod::FighterContent& fighter : lists.fighters) {
		if (fighter.costumes.empty()) return fighter.code + " has no costume";
		for (const mod::CostumeContent& costume : fighter.costumes) {
			if (costume.colours.empty()) return fighter.code + " costume " + std::to_string(costume.id) + " has no colour";
			if (costume.id < 0 || costume.id > 0xFF) return fighter.code + " has a costume number past a byte";
			for (int colour : costume.colours) {
				if (colour < 0 || colour > 0xFF) return fighter.code + " has a colour number past a byte";
			}
		}
	}
	return {};
}

// One past the highest costume and colour number of any fighter: the
// numbers a room shows (GameCatalog's costumeLimit and colorLimit).
void FillLimits()
{
	for (const mod::FighterContent& fighter : s_lists.fighters) {
		for (const mod::CostumeContent& costume : fighter.costumes) {
			s_catalog.costumeLimit = std::max(s_catalog.costumeLimit, costume.id + 1);
			for (int colour : costume.colours) s_catalog.colorLimit = std::max(s_catalog.colorLimit, colour + 1);
		}
	}
}

std::string CodeAt(const std::vector<std::string>& codes, std::int64_t id)
{
	return id >= 0 && id < (std::int64_t)codes.size() ? codes[(size_t)id] : std::string();
}

// The game's codes are ASCII capitals and digits.
std::wstring WideCode(const std::string& code)
{
	return std::wstring(code.begin(), code.end());
}

} // namespace

bool FillCatalog(const mod::ContentLists& lists)
{
	if (s_sealed) {
		mod::Log("catalog: refused a fill after Ember's runtime started (%zu fighters, %zu stages)", lists.fighters.size(),
			lists.stages.size());
		return false;
	}
	if (s_catalog.fighterCount > 0) return ContentFingerprint(lists) == ContentFingerprint(s_lists);
	if (const std::string problem = PickSpaceProblem(lists); !problem.empty()) {
		mod::Log("catalog: the game's lists make no pick space: %s", problem.c_str());
		return false;
	}
	s_lists = lists;
	s_buildId = std::string(kMatchBuild) + "/" + ContentFingerprint(lists);
	for (int id = 0; id < (int)s_lists.fighters.size(); id++) s_rosterOrder.push_back(id);
	s_catalog.rosterOrder = s_rosterOrder.data();
	FillRules();
	FillLimits();
	s_catalog.fighterCount = (int)s_lists.fighters.size();
	return true;
}

void SealCatalog()
{
	s_sealed = true;
}

int StageCount()
{
	return (int)s_lists.stages.size();
}

const std::string& MatchBuildId()
{
	return s_buildId;
}

std::string ContentFingerprint(const mod::ContentLists& lists)
{
	// Every list by its length and every code by its length, so no two pick
	// spaces read alike: the fighters, each with its costumes' numbers and
	// each costume's colours; then the stages.
	std::string text = std::to_string(lists.fighters.size()) + ";";
	for (const mod::FighterContent& fighter : lists.fighters) {
		text += std::to_string(fighter.code.size()) + ":" + fighter.code + std::to_string(fighter.costumes.size()) + "(";
		for (const mod::CostumeContent& costume : fighter.costumes) {
			text += std::to_string(costume.id) + "[";
			for (int colour : costume.colours) text += std::to_string(colour) + ",";
			text += "]";
		}
		text += ")";
	}
	text += std::to_string(lists.stages.size()) + ";";
	for (const std::string& code : lists.stages) text += std::to_string(code.size()) + ":" + code;
	char hex[17];
	std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)XXH3_64bits(text.data(), text.size()));
	return hex;
}

std::string FighterCode(std::int64_t fighter)
{
	const mod::FighterContent* content = FighterAt(fighter);
	return content ? content->code : std::string();
}

std::string StageCode(std::int64_t stage)
{
	return CodeAt(s_lists.stages, stage);
}

std::string StageName(std::int64_t stage)
{
	const std::string name = CodeAt(s_lists.stageNames, stage);
	return name.empty() ? StageCode(stage) : name;
}

std::vector<int> CostumeIds(std::int64_t fighter, bool installedOnly)
{
	std::vector<int> ids;
	if (const mod::FighterContent* content = FighterAt(fighter)) {
		for (const mod::CostumeContent& costume : content->costumes) {
			if (!installedOnly || costume.installed) ids.push_back(costume.id);
		}
	}
	return ids;
}

std::vector<int> ColourIds(std::int64_t fighter, int costume)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	return content ? content->colours : std::vector<int>();
}

std::string CostumeName(std::int64_t fighter, int costume)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	return content && !content->name.empty() ? content->name : std::to_string(costume);
}

std::string ColourName(int colour)
{
	// The costume settings menu's own labels (rva 0x553B80): "%02d" of the
	// number plus one, and "EX%02d" of the number less 99 from 100 on.
	char text[8];
	std::snprintf(text, sizeof(text), colour >= 100 ? "EX%02d" : "%02d", colour >= 100 ? colour - 99 : colour + 1);
	return text;
}

std::optional<int> FighterId(const std::wstring& code)
{
	for (size_t id = 0; id < s_lists.fighters.size(); id++) {
		if (WideCode(s_lists.fighters[id].code) == code) return (int)id;
	}
	return std::nullopt;
}

std::optional<int> StageId(const std::wstring& code)
{
	for (size_t id = 0; id < s_lists.stages.size(); id++) {
		if (WideCode(s_lists.stages[id]) == code) return (int)id;
	}
	return std::nullopt;
}

std::optional<mod::MatchPicks> LaunchPicks(const RoomMatch& match)
{
	if (!ValidPick(match.picks[0], false) || !ValidPick(match.picks[1], false) || !ValidStage(match.stage)) {
		return std::nullopt;
	}
	const std::optional<int> clock = mod::RoundClockFrames(match.roundSeconds);
	if (!mod::PlayableRounds(match.rounds) || !clock) return std::nullopt;
	mod::MatchPicks picks;
	for (int side = 0; side < 2; side++) {
		const sf4e::selection::Pick& pick = match.picks[side];
		picks.players[side] = {WideCode(FighterCode(pick.fighter)), pick.costume, pick.color};
	}
	picks.rules.stage = WideCode(StageCode(match.stage));
	picks.rules.rounds = match.rounds;
	picks.rules.timeFrames = *clock;
	return picks;
}

} // namespace sfve::adapter

namespace ember {
const GameCatalog& Catalog() { return sfve::adapter::s_catalog; }
} // namespace ember
