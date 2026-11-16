#include "ember/SfvPick.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPickMemory.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"

#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <optional>

namespace sfve::adapter {

namespace {

// Recursive: an UpdatePick edit may call this file's other functions
// (LookFor, RememberedLook, SetPickRandom).
std::recursive_mutex s_mutex;
PlayerPick s_pick;
std::optional<PlayerPick> s_dummy; // none until the player picks one
// Random on the board.
bool s_random = false;
// Eleven, which is Random's rolled fighter in the Eleven look (s_random is
// then on as well).
bool s_eleven = false;
// The look last stored for each fighter and the fighter last chosen
// (SetPick), saved between runs by SfvPickStore. The revision counts what
// changed of it that is saved; `s_touched` is a pick stored since the start,
// after which a restore is merged under this run's memory.
picks::PickMemory s_memory;
std::uint64_t s_revision = 0;
bool s_touched = false;
// What the stored picks were fitted to: the catalog, which is filled once and
// never changes after (FillCatalog). A pick stored before it was usable is
// fitted at the first read after, and not again.
bool s_fittedToCatalog = false;
// Rises with each change to what the stored pick, Random and Eleven say (the
// pick's own copy, the remembered looks, the two flags), for what is made
// from them and kept (SfvRandomFighter's note to the runtime).
std::uint64_t s_changes = 0;

// The pick under the catalog's rules and fitted to what this copy's player
// can ready (FitOwnPick), and its stage fitted to the stage row's choices
// (FittedStage, over StageOffered, the one rule for their own stage offers);
// as it is until the catalog is filled.
PlayerPick Fitted(PlayerPick pick)
{
	if (!ember::Usable(ember::Catalog())) return pick;
	FitOwnPick(pick.pick);
	pick.stage = FittedStage(pick.stage, PlayableIds(StageOptions({})));
	return pick;
}

// The look the fighter is in when it is chosen, as this player can wear it:
// its remembered look; or, for a fighter not stored yet, or one whose
// remembered look this player cannot wear (a costume or colour they do not
// own or this copy has not installed, a loadout the fighter lacks), the
// fighter's default look (picks::UnchosenLook, fitted to what they own) in
// the model size `current` has. With no catalog the look as it was.
sf4e::selection::Pick WornLook(const std::optional<sf4e::selection::Pick>& remembered, int fighter,
	const sf4e::selection::Pick& current)
{
	if (!ember::ValidFighter(fighter)) {
		sf4e::selection::Pick look = remembered.value_or(current);
		look.fighter = fighter;
		return look;
	}
	if (remembered) {
		sf4e::selection::Pick look = *remembered;
		look.fighter = fighter;
		FitLookWithin(look);
		if (picks::SameFitting(look, *remembered)) return look;
	}
	sf4e::selection::Pick look = picks::UnchosenLook(fighter);
	look.size = current.size;
	FitLookWithin(look);
	return look;
}

// Under the lock: the stored picks fitted, once the catalog can fit them.
void FitStoredLocked()
{
	if (s_fittedToCatalog || !ember::Usable(ember::Catalog())) return;
	s_fittedToCatalog = true;
	s_pick = Fitted(s_pick);
	if (s_dummy) {
		s_dummy = Fitted(*s_dummy);
		s_dummy->stage = 0;
	}
	s_changes++;
}

// Why the asked costume is not the stored one, for the log: what the
// catalog says of it for this copy's player.
const char* CostumeState(const sf4e::selection::Pick& asked)
{
	const std::vector<int> ready = CostumeIds(asked.fighter, true);
	if (std::find(ready.begin(), ready.end(), asked.costume) != ready.end()) return "ready here";
	const std::vector<int> all = CostumeIds(asked.fighter, false);
	if (std::find(all.begin(), all.end(), asked.costume) == all.end()) return "not in the game's spec";
	return CostumeNotOwned(asked.fighter, asked.costume) ? "not owned by this account" : "not installed here";
}

// A pick stored as another look than the one asked for (the fitting moved
// its fighter, costume or colour) is said in the log, with the catalog's
// view of the asked costume, so a player's report of a look that "changes
// by itself" can be read from their log.
void LogRefitted(const char* which, const PlayerPick& asked, const PlayerPick& stored)
{
	const sf4e::selection::Pick& a = asked.pick;
	const sf4e::selection::Pick& s = stored.pick;
	if (a.fighter == s.fighter && a.costume == s.costume && a.color == s.color) return;
	mod::Log("pick: %s %s costume %d colour %d (costume %s) stored as %s costume %d colour %d", which,
		FighterCode(a.fighter).c_str(), a.costume, a.color, CostumeState(a), FighterCode(s.fighter).c_str(), s.costume,
		s.color);
}

} // namespace

PlayerPick CurrentPick()
{
	std::lock_guard lock(s_mutex);
	FitStoredLocked();
	return s_pick;
}

std::uint64_t PickChanges()
{
	std::lock_guard lock(s_mutex);
	FitStoredLocked();
	return s_changes;
}

PlayerPick SetPick(const PlayerPick& pick)
{
	std::lock_guard lock(s_mutex);
	s_pick = Fitted(pick);
	LogRefitted("asked for", pick, s_pick);
	s_touched = true;
	s_changes++;
	// Eleven's looks belong to Eleven, not to the fighters it rolled, and a
	// fighter Random rolled is not one the player chose.
	bool changed = !s_eleven && s_memory.RememberLook(s_pick.pick);
	if (!s_random) changed = s_memory.Choose(s_pick.pick.fighter) || changed;
	if (changed) s_revision++;
	return s_pick;
}

PlayerPick UpdatePick(const std::function<bool(PlayerPick&)>& edit)
{
	std::lock_guard lock(s_mutex);
	PlayerPick pick = CurrentPick();
	if (!edit(pick)) return pick;
	return SetPick(pick);
}

PlayerPick ChooseFighter(int fighter, const std::function<void(PlayerPick&)>& then, Whose whose)
{
	std::lock_guard lock(s_mutex);
	if (whose == Whose::Dummy) {
		PlayerPick dummy = CurrentDummy();
		dummy.pick = LookFor(dummy.pick, fighter, Whose::Dummy);
		if (then) then(dummy);
		return SetDummy(dummy);
	}
	// Before the store, so it is the fighter chosen and not a roll.
	SetPickRandom(false);
	return UpdatePick([&](PlayerPick& pick) {
		pick.pick = LookFor(pick.pick, fighter, true);
		if (then) then(pick);
		return true;
	});
}

PlayerPick CurrentDummy()
{
	std::lock_guard lock(s_mutex);
	FitStoredLocked();
	PlayerPick dummy = s_dummy.value_or(s_pick);
	dummy.stage = 0;
	return dummy;
}

PlayerPick SetDummy(const PlayerPick& dummy)
{
	std::lock_guard lock(s_mutex);
	s_dummy = Fitted(dummy);
	LogRefitted("dummy asked for", dummy, *s_dummy);
	s_dummy->stage = 0;
	// The dummy's own look for its fighter, saved with the player's.
	s_touched = true;
	if (s_memory.RememberLook(s_dummy->pick, Whose::Dummy)) s_revision++;
	return *s_dummy;
}

void ForgetDummyForTest()
{
	std::lock_guard lock(s_mutex);
	s_dummy.reset();
}

void ForgetPickMemoryForTest()
{
	std::lock_guard lock(s_mutex);
	s_memory = {};
	s_revision = 0;
	s_touched = false;
	s_changes++;
}

sf4e::selection::Pick WirePick(sf4e::selection::Pick pick, bool roomAllowsTiny)
{
	// The player's own voice for the fighter goes with the pick, so the
	// opponent's copy plays it too (picks::VoiceIn, SfvCatalog's LaunchPicks).
	picks::SetVoice(pick, OwnVoice(pick.fighter));
	// The costume code the player chose, with the marker that says this copy
	// knows codes even for none (picks::LoadCodeIn, SfvCatalog's LaunchPicks).
	picks::SetLoadCode(pick, picks::LoadCodeIn(pick).value_or(0));
	// The size only where the room allows tiny fighters (a Training or Home
	// pick is never sent, and a room without the rule sees 0).
	pick.size = SizeToSend(pick.size, roomAllowsTiny);
	return pick;
}

void ToWire(const sf4e::selection::Pick& pick, bool roomAllowsTiny, sf4e::wire::ConfirmedCharaConditions& out)
{
	sf4e::selection::ToNative(WirePick(pick, roomAllowsTiny), out);
}

void AddPickTo(sf4e::commands::Ready& ready)
{
	const PlayerPick pick = CurrentPick();
	const auto runtime = sf4e::NetplayFacade::GetRuntimeSnapshotShared();
	ready.pick = WirePick(pick.pick, runtime && runtime->Room().tinyFighters);
	ready.stage = pick.stage;
}

bool PickRandom()
{
	std::lock_guard lock(s_mutex);
	return s_random;
}

void SetPickRandom(bool random)
{
	std::lock_guard lock(s_mutex);
	if (s_random != random || s_eleven) mod::Log("pick: Random %s", random ? "on" : "off");
	s_random = random;
	s_eleven = false;
	s_changes++;
}

bool PickEleven()
{
	std::lock_guard lock(s_mutex);
	return s_eleven;
}

void SetPickEleven(bool eleven)
{
	std::lock_guard lock(s_mutex);
	if (s_eleven != eleven) mod::Log("pick: Eleven %s", eleven ? "on" : "off");
	s_eleven = eleven;
	s_random = eleven;
	s_changes++;
}

std::optional<sf4e::selection::Pick> RememberedLook(int fighter, Whose whose)
{
	std::lock_guard lock(s_mutex);
	return s_memory.LookOf(fighter, whose);
}

sf4e::selection::Pick LookFor(const sf4e::selection::Pick& pick, int fighter, Whose whose)
{
	return WornLook(RememberedLook(fighter, whose), fighter, pick);
}

sf4e::selection::Pick LookFor(const sf4e::selection::Pick& pick, int fighter, bool remembered)
{
	if (remembered) return LookFor(pick, fighter, Whose::Player);
	sf4e::selection::Pick look = pick;
	look.fighter = fighter;
	if (ember::ValidFighter(fighter)) FitLookWithin(look);
	return look;
}

bool RestorePickMemory(const picks::PickMemory& saved)
{
	std::lock_guard lock(s_mutex);
	if (s_touched) {
		if (s_memory.MergeUnder(saved)) s_changes++;
		return false;
	}
	s_memory = saved;
	s_changes++;
	if (const std::optional<int> chosen = s_memory.ChosenFighter()) {
		s_pick.pick = WornLook(s_memory.LookOf(*chosen), *chosen, s_pick.pick);
		s_pick = Fitted(s_pick);
	}
	return true;
}

picks::PickMemory PickMemoryNow(std::uint64_t& revision)
{
	std::lock_guard lock(s_mutex);
	revision = s_revision;
	return s_memory;
}

int SizeToSend(int storedSize, bool roomAllowsTiny)
{
	return roomAllowsTiny && storedSize == picks::kSizeTiny ? picks::kSizeTiny : picks::kSizeNormal;
}

std::string PickText(const PlayerPick& pick)
{
	return FighterCode(pick.pick.fighter) + " costume " + std::to_string(pick.pick.costume) + " colour " +
		std::to_string(pick.pick.color) + " " + picks::LoadoutText(picks::LoadoutOf(pick.pick)) +
		(pick.pick.size == picks::kSizeTiny ? " size tiny" : "") + " stage " +
		(pick.stage == picks::kRandomStage ? "random" : StageCode(pick.stage));
}

int FittedStage(int stage, const std::vector<int>& choices)
{
	if (std::find(choices.begin(), choices.end(), stage) != choices.end()) return stage;
	const auto first = std::find_if(choices.begin(), choices.end(), [](int id) { return id != picks::kRandomStage; });
	return first == choices.end() ? picks::kRandomStage : *first;
}

NamedStage NameStage(const std::wstring& code, int stage)
{
	if (code.empty()) return {stage};
	if (code == L"RND") return {picks::kRandomStage};
	const std::optional<int> id = StageId(code);
	if (!id) return {stage, StageRefusal::NotInGame};
	if (!StageInstalled(*id)) return {stage, StageRefusal::NotInstalled};
	if (!StageOffered(*id)) return {stage, StageRefusal::NotOffered};
	return {*id};
}

} // namespace sfve::adapter
