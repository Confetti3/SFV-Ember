#include "ember/SfvCatalog.hxx"

#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

// Written once by FillCatalog, before the runtime's threads read the catalog:
// the game's lists, and their pick space, which decides every pick's
// validity as the room host's copy of it does (SfvPickSpace).
mod::ContentLists s_lists;
picks::PickSpace s_space;
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

bool Contains(const std::vector<int>& list, int value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

bool HasColour(const mod::CostumeContent& costume, int colour)
{
	return Contains(costume.colours, colour);
}

// What this copy's player may pick, by the game's answer (GameContent's
// `owned`) while the gate is on. Only this copy's own player's picks are
// judged so: the opponent's pick plays as picked (LaunchPicks checks the
// spec alone), as SF4's DLC gate does.
bool FighterOwned(const mod::FighterContent& fighter)
{
	return !s_gate || fighter.owned;
}

// What a player wears is a look, a costume in one of its colours, and the
// game's answer is owned only for the look as a whole: the costume and that
// colour both theirs. A costume of theirs in no colour of theirs gives them
// nothing to wear, so no rule here asks a costume's or a colour's flag alone.
bool LookOwned(const mod::CostumeContent& costume, int colour)
{
	return !s_gate || (costume.owned && Contains(costume.ownedColours, colour));
}

// A costume with at least one look the game lets them pick, installed here or not.
bool CostumeOwned(const mod::CostumeContent& costume)
{
	return std::any_of(costume.colours.begin(), costume.colours.end(),
		[&costume](int colour) { return LookOwned(costume, colour); });
}

// A look this copy's player can ready: a colour of the costume, the costume
// installed here, and the look theirs. The one availability rule: the Ready
// check, the fitting of their own pick and the pick screen's choices are all
// built from it.
bool LookReady(const mod::CostumeContent& costume, int colour)
{
	return costume.installed && HasColour(costume, colour) && LookOwned(costume, colour);
}

// The costume's colours, in the game's order, that make a look they can ready.
std::vector<int> ReadyColours(const mod::CostumeContent& costume)
{
	std::vector<int> ready;
	for (int colour : costume.colours) {
		if (LookReady(costume, colour)) ready.push_back(colour);
	}
	return ready;
}

// A costume with at least one look they can ready.
bool CostumeReady(const mod::CostumeContent& costume)
{
	return std::any_of(costume.colours.begin(), costume.colours.end(),
		[&costume](int colour) { return LookReady(costume, colour); });
}

// A costume of the fighter the spec has, in one of that costume's colours.
bool ValidPick(const sf4e::selection::Pick& pick, bool)
{
	return picks::ValidPick(s_space, pick);
}

bool ValidStage(std::int64_t id)
{
	return picks::ValidStage(s_space, id);
}

// This copy's player cannot play a fighter whose package this copy lacks,
// whatever the spec says (GameContent's `installed`), nor, with the gate on,
// one the game does not let them pick (`owned`), nor one with no look they
// can ready. SFV's catalog reads its own table for all three;
// `availability` (SfvGame's FighterAvailability, made from the same table by
// OwnedAvailability) adds nothing to it.
bool FighterLocked(int fighter, const sf4e::selection::Availability&)
{
	const mod::FighterContent* content = FighterAt(fighter);
	return !content || !content->installed || !FighterOwned(*content) ||
		std::none_of(content->costumes.begin(), content->costumes.end(), CostumeReady);
}

// A valid pick this copy's player can ready: the fighter not locked and the
// look ready. The runtime's Ready check asks this for this copy's own player
// only.
bool AvailablePick(const sf4e::selection::Pick& pick, bool editionSelect, const sf4e::selection::Availability& availability,
	const sf4e::selection::InstalledContent*)
{
	return ValidPick(pick, editionSelect) && !FighterLocked(pick.fighter, availability) &&
		LookReady(*CostumeAt(pick.fighter, pick.costume), pick.color);
}

// This copy's player's own pick, made one they can ready: the first fighter
// of the roster's order they may play, that fighter's first costume with a
// look they can ready, that costume's first colour making one. A part
// already theirs stays. A fighter not locked always has such a costume, and
// such a costume such a colour, so the pick ends ready; only with no fighter
// they may play does it stay as it is (and a Ready of it is refused). No
// ownership gates a loadout (SFV's own menus gate none): only the fighter's
// own count does.
void FitOwned(sf4e::selection::Pick& pick)
{
	if (FighterLocked(pick.fighter, {})) {
		const auto open = std::find_if(s_rosterOrder.begin(), s_rosterOrder.end(), [](int id) { return !FighterLocked(id, {}); });
		if (open == s_rosterOrder.end()) return;
		pick.fighter = *open;
	}
	const mod::FighterContent& fighter = *FighterAt(pick.fighter);
	const mod::CostumeContent* costume = CostumeAt(pick.fighter, pick.costume);
	if (!costume || !CostumeReady(*costume)) {
		costume = &*std::find_if(fighter.costumes.begin(), fighter.costumes.end(), CostumeReady);
		pick.costume = costume->id;
	}
	if (!LookReady(*costume, pick.color)) pick.color = ReadyColours(*costume).front();
	// The loadout through the fighter's change, as picks::FitPick keeps it:
	// each part the new fighter lacks becomes I.
	picks::SetLoadout(pick, picks::FitLoadout(*picks::FighterAt(s_space, pick.fighter), picks::LoadoutOf(pick)));
}

// Each part SFV refuses becomes the game's first of its kind, in this order
// so that each is fitted to the one before it: fighter 0; the fighter's first
// costume; that costume's first colour; each loadout part the fighter lacks
// I; no handicap. Only the spec decides, so every copy fits a pick the same
// way. With `availability` given the pick is this copy's own player's (their
// stored pick, a Random roll's look), and is then also fitted to what they
// can ready (FitOwned), which is per copy.
bool NormalizePick(sf4e::selection::Pick& pick, bool, const sf4e::selection::Availability* availability,
	const sf4e::selection::InstalledContent*)
{
	const sf4e::selection::Pick before = pick;
	picks::FitPick(s_space, pick);
	if (availability && FighterAt(pick.fighter)) FitOwned(pick);
	return !picks::SameFitting(pick, before);
}

// Random (kRandomStage) becomes a stage of the universe that is not
// `unavailable`, each as likely (the roll modulo their count: a 32-bit roll
// over at most a few dozen stages is even to within one part in 10^7). The
// runtime's set already holds this copy's own missing stages, so nothing else
// is asked. With no stage left, Random stays as it is, unresolved, and the
// runtime's Ready check refuses it. Any other choice is a stage of the
// universe, else the first. SF4's RandomPool and custom stages are not SFV's.
int ResolveStage(int choice, std::uint32_t roll, const sf4e::selection::RandomPool&, const sf4e::selection::InstalledContent*,
	const std::vector<std::int64_t>& unavailable)
{
	if (choice != picks::kRandomStage) return ValidStage(choice) ? choice : 0;
	std::vector<int> candidates;
	for (int id = 0; id < (int)s_lists.stages.size(); id++) {
		if (std::find(unavailable.begin(), unavailable.end(), id) == unavailable.end()) candidates.push_back(id);
	}
	return candidates.empty() ? choice : candidates[roll % candidates.size()];
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
	return picks::CostumeCount(s_space, fighter);
}

// Empty until FillCatalog. The public rooms setup card's faces are the first
// four fighters of the roster. The Ultra numbers are the loadouts.
constinit ember::GameCatalog s_catalog = {0, 0, 0, picks::kLoadoutChoices, &ValidPick, &ValidStage, {0, 1, 2, 3}, picks::kRandomStage, &FighterLocked,
	&AvailablePick, &NormalizePick, &ResolveStage, &FighterName, &CostumeCount, nullptr, nullptr, 0, nullptr, 0, false,
	true};

// The room's round counts and times SFV plays, so a table offers no other.
void FillRules()
{
	s_roundCounts.assign(std::begin(picks::kRoundCounts), std::end(picks::kRoundCounts));
	s_roundTimes.assign(std::begin(picks::kRoundTimes), std::end(picks::kRoundTimes));
	s_catalog.roundCounts = s_roundCounts.data();
	s_catalog.roundCountChoices = (int)s_roundCounts.size();
	s_catalog.roundTimes = s_roundTimes.data();
	s_catalog.roundTimeChoices = (int)s_roundTimes.size();
}

// One past the highest costume and colour number of any fighter: the
// numbers a room shows (GameCatalog's costumeLimit and colorLimit).
void FillLimits()
{
	s_catalog.costumeLimit = picks::CostumeLimit(s_space);
	s_catalog.colorLimit = picks::ColourLimit(s_space);
}

const mod::StageContent* StageAt(std::int64_t stage)
{
	return stage >= 0 && stage < (std::int64_t)s_lists.stages.size() ? &s_lists.stages[(size_t)stage] : nullptr;
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
	picks::PickSpace space = PickSpaceOf(lists);
	if (const std::string problem = picks::Problem(space); !problem.empty()) {
		mod::Log("catalog: the game's lists make no pick space: %s", problem.c_str());
		return false;
	}
	s_lists = lists;
	s_space = std::move(space);
	s_gate = ownershipGate;
	s_buildId = picks::BuildId(s_space);
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

picks::PickSpace PickSpaceOf(const mod::ContentLists& lists)
{
	picks::PickSpace space;
	for (const mod::FighterContent& fighter : lists.fighters) {
		picks::Fighter entry{fighter.code, {}, (int)fighter.vSkills.size(), (int)fighter.vTriggers.size()};
		for (const mod::CostumeContent& costume : fighter.costumes) entry.costumes.push_back({costume.id, costume.colours});
		space.fighters.push_back(std::move(entry));
	}
	for (const mod::StageContent& stage : lists.stages) space.stages.push_back(stage.code);
	return space;
}

std::string ContentFingerprint(const mod::ContentLists& lists)
{
	return picks::Fingerprint(PickSpaceOf(lists));
}

std::string PickSpaceJson()
{
	return s_catalog.fighterCount > 0 ? picks::ToJson(s_space, s_buildId) : std::string();
}

std::string FighterCode(std::int64_t fighter)
{
	const mod::FighterContent* content = FighterAt(fighter);
	return content ? content->code : std::string();
}

std::string StageCode(std::int64_t stage)
{
	const mod::StageContent* content = StageAt(stage);
	return content ? content->code : std::string();
}

std::string StageName(std::int64_t stage)
{
	const mod::StageContent* content = StageAt(stage);
	if (!content) return {};
	return content->name.empty() ? content->code : content->name;
}

bool StageInstalled(std::int64_t stage)
{
	const mod::StageContent* content = StageAt(stage);
	return content && content->installed;
}

std::vector<std::int64_t> MissingStages()
{
	std::vector<std::int64_t> missing;
	for (std::int64_t id = 0; id < (std::int64_t)s_lists.stages.size(); id++) {
		if (!StageInstalled(id)) missing.push_back(id);
	}
	return missing;
}

std::vector<int> StageChoices(const std::vector<std::int64_t>& unavailable)
{
	std::vector<int> stages{picks::kRandomStage};
	for (int id = 0; id < StageCount(); id++) {
		if (std::find(unavailable.begin(), unavailable.end(), id) == unavailable.end()) stages.push_back(id);
	}
	return stages;
}

std::vector<int> CostumeIds(std::int64_t fighter, bool pickableOnly)
{
	std::vector<int> ids;
	if (const mod::FighterContent* content = FighterAt(fighter)) {
		for (const mod::CostumeContent& costume : content->costumes) {
			if (!pickableOnly || CostumeReady(costume)) ids.push_back(costume.id);
		}
	}
	return ids;
}

std::vector<int> ColourIds(std::int64_t fighter, int costume, bool pickableOnly)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	if (!content) return {};
	return pickableOnly ? ReadyColours(*content) : content->colours;
}

bool FighterNotOwned(std::int64_t fighter)
{
	const mod::FighterContent* content = FighterAt(fighter);
	return content &&
		(!FighterOwned(*content) || std::none_of(content->costumes.begin(), content->costumes.end(), CostumeOwned));
}

bool CostumeNotOwned(std::int64_t fighter, int costume)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	return content && !CostumeOwned(*content);
}

bool ColourNotOwned(std::int64_t fighter, int costume, int colour)
{
	const mod::CostumeContent* content = CostumeAt(fighter, costume);
	return content && HasColour(*content, colour) && !LookOwned(*content, colour);
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

int PartChoices(std::int64_t fighter, picks::LoadoutPart part)
{
	const picks::Fighter* content = picks::FighterAt(s_space, fighter);
	return content ? picks::PartChoices(*content, part) : 0;
}

std::string MoveName(std::int64_t fighter, picks::LoadoutPart part, int number)
{
	const mod::FighterContent* content = FighterAt(fighter);
	if (!content) return {};
	const std::vector<std::string>& names = part == picks::LoadoutPart::VSkill ? content->vSkills : content->vTriggers;
	return number >= 0 && number < (int)names.size() ? names[(size_t)number] : std::string();
}

std::vector<int> LoadoutIndices(std::int64_t fighter)
{
	const picks::Fighter* content = picks::FighterAt(s_space, fighter);
	return content ? picks::LoadoutIndices(*content) : std::vector<int>();
}

mod::FighterPick FighterPickOf(const sf4e::selection::Pick& pick)
{
	return {WideCode(FighterCode(pick.fighter)), pick.costume, pick.color, picks::LoadoutOf(pick)};
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
		if (WideCode(s_lists.stages[id].code) == code) return (int)id;
	}
	return std::nullopt;
}

std::string LaunchRefusal(const RoomMatch& match)
{
	for (int side = 0; side < 2; side++) {
		const sf4e::selection::Pick& pick = match.picks[side];
		const std::string who = "P" + std::to_string(side + 1) + "'s ";
		if (!ValidPick(pick, false)) return who + "pick is not one of the game's";
		// The spec has it; this copy must have its package too, or the battle
		// would load nothing sensible.
		const mod::FighterContent& fighter = *FighterAt(pick.fighter);
		if (!fighter.installed) return who + "fighter " + fighter.code + " is not installed here";
		if (!CostumeAt(pick.fighter, pick.costume)->installed) {
			return who + fighter.code + " costume " + std::to_string(pick.costume) + " is not installed here";
		}
	}
	if (!ValidStage(match.stage)) return "the stage is not one of the game's";
	if (!StageInstalled(match.stage)) return "stage " + StageCode(match.stage) + " is not installed here";
	// A Practice room projects the longest round time (room::MatchRoundTime),
	// which SFV plays as no time limit (RoomClockFrames).
	if (!mod::PlayableRounds(match.rounds) || !mod::RoomClockFrames(match.roundSeconds)) {
		return "SFV cannot play " + std::to_string(match.rounds) + " rounds of " + std::to_string(match.roundSeconds) + " s";
	}
	return {};
}

std::optional<mod::MatchPicks> LaunchPicks(const RoomMatch& match)
{
	if (!LaunchRefusal(match).empty()) return std::nullopt;
	const int clock = *mod::RoomClockFrames(match.roundSeconds);
	mod::MatchPicks picks;
	for (int side = 0; side < 2; side++) picks.players[side] = FighterPickOf(match.picks[side]);
	picks.rules.stage = WideCode(StageCode(match.stage));
	picks.rules.rounds = match.rounds;
	picks.rules.timeFrames = clock;
	picks.practice = match.practice;
	return picks;
}

} // namespace sfve::adapter

namespace ember {
const GameCatalog& Catalog() { return sfve::adapter::s_catalog; }
} // namespace ember
