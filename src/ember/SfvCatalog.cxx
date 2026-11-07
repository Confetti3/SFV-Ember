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
// Ember's ownership gate (FillCatalog's ownershipGate): this copy's player
// may pick only what the game lets them. Off only while developing.
bool s_gate = true;
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

// What this copy's player may pick, by the game's answer (GameContent's
// `owned`) while the gate is on. Only this copy's own player's picks are
// judged so: the opponent's pick plays as picked (LaunchPicks checks the
// spec alone), as SF4's DLC gate does.
bool FighterOwned(const mod::FighterContent& fighter)
{
	return !s_gate || fighter.owned;
}

bool CostumeOwned(const mod::CostumeContent& costume)
{
	return !s_gate || costume.owned;
}

bool ColourOwned(const mod::CostumeContent& costume, int colour)
{
	return !s_gate ||
		std::find(costume.ownedColours.begin(), costume.ownedColours.end(), colour) != costume.ownedColours.end();
}

// A costume this copy's player can ready: installed here and theirs.
bool CostumePickable(const mod::CostumeContent& costume)
{
	return costume.installed && CostumeOwned(costume);
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

// This copy's player cannot play a fighter whose package this copy lacks,
// whatever the spec says (GameContent's `installed`), nor, with the gate on,
// one the game does not let them pick (`owned`). SFV's catalog reads its own
// table for both; `availability` (SfvGame's FighterAvailability, made from
// the same table by OwnedAvailability) adds nothing to it.
bool FighterLocked(int fighter, const sf4e::selection::Availability&)
{
	const mod::FighterContent* content = FighterAt(fighter);
	return !content || !content->installed || !FighterOwned(*content);
}

// A valid pick this copy's player can ready: the fighter not locked, the
// costume installed here and, with the gate on, the costume and the colour
// theirs. The runtime's Ready check asks this for this copy's own player
// only.
bool AvailablePick(const sf4e::selection::Pick& pick, bool editionSelect, const sf4e::selection::Availability& availability,
	const sf4e::selection::InstalledContent*)
{
	if (!ValidPick(pick, editionSelect) || FighterLocked(pick.fighter, availability)) return false;
	const mod::CostumeContent* costume = CostumeAt(pick.fighter, pick.costume);
	return CostumePickable(*costume) && ColourOwned(*costume, pick.color);
}

// This copy's player's own pick, made one they can ready where they can:
// the first fighter of the roster's order they may play, that fighter's
// first costume they may ready, that costume's first colour of theirs. A
// part already theirs stays; one with no such choice stays as it is (and a
// Ready of it is refused).
void FitOwned(sf4e::selection::Pick& pick)
{
	if (FighterLocked(pick.fighter, {})) {
		for (int id : s_rosterOrder) {
			if (FighterLocked(id, {})) continue;
			pick.fighter = id;
			break;
		}
	}
	const mod::FighterContent* fighter = FighterAt(pick.fighter);
	if (!fighter) return;
	const mod::CostumeContent* costume = CostumeAt(pick.fighter, pick.costume);
	if (!costume || !CostumePickable(*costume)) {
		for (const mod::CostumeContent& candidate : fighter->costumes) {
			if (!CostumePickable(candidate)) continue;
			costume = &candidate;
			pick.costume = candidate.id;
			break;
		}
	}
	if (!costume) return;
	if (!HasColour(*costume, pick.color) || !ColourOwned(*costume, pick.color)) {
		for (int colour : costume->colours) {
			if (!ColourOwned(*costume, colour)) continue;
			pick.color = colour;
			break;
		}
	}
}

// Each part SFV refuses becomes the game's first of its kind, in this order
// so that each is fitted to the one before it: fighter 0; the fighter's first
// costume; that costume's first colour; no Ultra and no handicap. Only the
// spec decides, so every copy fits a pick the same way. With `availability`
// given the pick is this copy's own player's (their stored pick, a Random
// roll's look), and is then also fitted to what they can ready (FitOwned),
// which is per copy.
bool NormalizePick(sf4e::selection::Pick& pick, bool, const sf4e::selection::Availability* availability,
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
	if (availability && fighter) FitOwned(pick);
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

bool FillCatalog(const mod::ContentLists& lists, bool ownershipGate)
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
	s_gate = ownershipGate;
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

std::vector<int> CostumeIds(std::int64_t fighter, bool pickableOnly)
{
	std::vector<int> ids;
	if (const mod::FighterContent* content = FighterAt(fighter)) {
		for (const mod::CostumeContent& costume : content->costumes) {
			if (!pickableOnly || CostumePickable(costume)) ids.push_back(costume.id);
		}
	}
	return ids;
}

std::vector<int> ColourIds(std::int64_t fighter, int costume, bool pickableOnly)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	std::vector<int> ids;
	if (!content) return ids;
	for (int colour : content->colours) {
		if (!pickableOnly || ColourOwned(*content, colour)) ids.push_back(colour);
	}
	return ids;
}

bool FighterNotOwned(std::int64_t fighter)
{
	const mod::FighterContent* content = FighterAt(fighter);
	return content && !FighterOwned(*content);
}

bool CostumeNotOwned(std::int64_t fighter, int costume)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	return content && !CostumeOwned(*content);
}

bool ColourNotOwned(std::int64_t fighter, int costume, int colour)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	return content && HasColour(*content, colour) && !ColourOwned(*content, colour);
}

sf4e::selection::Availability OwnedAvailability(std::int64_t fighter)
{
	sf4e::selection::Availability availability;
	if (!FighterAt(fighter) || FighterLocked((int)fighter, {})) return availability;
	availability.ready = true;
	availability.costumes = ~0u;
	availability.colors.fill(~0u);
	return availability;
}

void FitOwnPick(sf4e::selection::Pick& pick)
{
	if (s_catalog.fighterCount == 0) return;
	const sf4e::selection::Availability availability = OwnedAvailability(pick.fighter);
	NormalizePick(pick, false, &availability, nullptr);
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
