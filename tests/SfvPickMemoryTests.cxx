// What the player's pick remembers (SfvPickMemory, SfvPick), checked without
// the game: each fighter's own costume, colour, V-Skill and V-Trigger, the
// fighter last chosen, and the saved form that carries them between runs,
// with a roster longer than SF4's 44 fighters.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickMemory.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvPickStore.hxx"
#include "ember/SfvRandomFighter.hxx"

#include "common/SelectionPick.hxx"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using picks::PickMemory;
using sf4e::selection::Pick;

namespace {

constexpr int kRyu = 0, kCnl = 1, kBsn = 4;
constexpr int kRosterSize = 60;
constexpr int kLateFighter = 55; // past SF4's 44 fighters

// SFV's roster is longer than SF4's: `kRosterSize` fighters F00 to F59, each
// with two costumes (numbers 0 and 3, colours 0 to 2 and 0 to 5), two
// V-Skills and a single V-Trigger, the first costume of fighter 7 unshipped.
picks::PickSpace LongSpace()
{
	picks::PickSpace space;
	for (int id = 0; id < kRosterSize; id++) {
		picks::Fighter fighter;
		fighter.code = (id < 10 ? "F0" : "F") + std::to_string(id);
		fighter.costumes = {{0, {0, 1, 2}, id == 7}, {3, {0, 1, 2, 3, 4, 5}, false}};
		fighter.vSkills = 2;
		fighter.vTriggers = 1;
		space.fighters.push_back(fighter);
	}
	space.stages = {"S01"};
	return space;
}

Pick LookOf(int fighter, int costume, int colour, picks::Loadout loadout = {})
{
	return PickOf(fighter, costume, colour, loadout);
}

bool SameLook(const Pick& a, const Pick& b)
{
	return a.fighter == b.fighter && a.costume == b.costume && a.color == b.color && picks::LoadoutOf(a) == picks::LoadoutOf(b);
}

// The saved form as the settings file holds it: written out and read in.
PickMemory ThroughFile(const PickMemory& memory, const picks::PickSpace& space)
{
	return PickMemory::FromJson(nlohmann::json::parse(memory.ToJson(space).dump()), space);
}

} // namespace

TEST_CASE("a memory knows each fighter's own look and the fighter chosen last")
{
	PickMemory memory;
	CHECK_FALSE(memory.LookOf(kRyu).has_value());
	CHECK_FALSE(memory.ChosenFighter().has_value());
	CHECK(memory.RememberLook(LookOf(kRyu, 3, 4, kTwoOne)));
	CHECK(memory.RememberLook(LookOf(kCnl, 0, 1, kOneOne)));
	CHECK(SameLook(*memory.LookOf(kRyu), LookOf(kRyu, 3, 4, kTwoOne)));
	CHECK(SameLook(*memory.LookOf(kCnl), LookOf(kCnl, 0, 1, kOneOne)));
	CHECK_FALSE(memory.LookOf(kBsn).has_value());
	// Only a change of what is saved counts: the costume, colour or loadout.
	CHECK_FALSE(memory.RememberLook(LookOf(kRyu, 3, 4, kTwoOne)));
	Pick tiny = LookOf(kRyu, 3, 4, kTwoOne);
	tiny.size = picks::kSizeTiny;
	CHECK_FALSE(memory.RememberLook(tiny));
	CHECK(memory.LookOf(kRyu)->size == picks::kSizeTiny);
	CHECK(memory.RememberLook(LookOf(kRyu, 3, 5, kTwoOne)));
	CHECK(memory.RememberLook(LookOf(kRyu, 3, 5, kTwoTwo)));
	// The chosen fighter.
	CHECK(memory.Choose(kCnl));
	CHECK_FALSE(memory.Choose(kCnl));
	CHECK(memory.ChosenFighter() == kCnl);
	// A fighter outside the roster is remembered nowhere.
	CHECK_FALSE(memory.RememberLook(LookOf(-1, 0, 0)));
	CHECK_FALSE(memory.RememberLook(LookOf(ember::MaxFighters, 0, 0)));
	CHECK_FALSE(memory.Choose(-1));
	CHECK(memory.ChosenFighter() == kCnl);
}

TEST_CASE("the saved picks come back as saved, a fighter above SF4's 44 included")
{
	const picks::PickSpace space = LongSpace();
	PickMemory memory;
	memory.RememberLook(LookOf(kLateFighter, 3, 5, picks::Loadout{{1, 0}}));
	memory.RememberLook(LookOf(2, 0, 2, picks::Loadout{{0, 0}}));
	memory.Choose(kLateFighter);
	const PickMemory read = ThroughFile(memory, space);
	REQUIRE(read.ChosenFighter().has_value());
	CHECK(*read.ChosenFighter() == kLateFighter);
	REQUIRE(read.LookOf(kLateFighter).has_value());
	CHECK(SameLook(*read.LookOf(kLateFighter), LookOf(kLateFighter, 3, 5, picks::Loadout{{1, 0}})));
	REQUIRE(read.LookOf(2).has_value());
	CHECK(SameLook(*read.LookOf(2), LookOf(2, 0, 2, picks::Loadout{{0, 0}})));
	CHECK_FALSE(read.LookOf(3).has_value());
	// Saved again, it is the same form.
	CHECK(read.ToJson(space) == memory.ToJson(space));
	// By code: the file names fighters by their codes.
	const nlohmann::json saved = memory.ToJson(space);
	CHECK(saved["fighter"] == "F55");
	CHECK(saved["looks"]["F55"]["costume"] == 3);
	CHECK(saved["looks"]["F55"]["colour"] == 5);
	CHECK(saved["looks"]["F55"]["vSkill"] == 1);
	CHECK(saved["looks"]["F55"]["vTrigger"] == 0);
}

TEST_CASE("a fighter keeps its look when the roster's numbers move: the file goes by code")
{
	const picks::PickSpace space = LongSpace();
	PickMemory memory;
	memory.RememberLook(LookOf(10, 3, 4));
	memory.Choose(10);
	const nlohmann::json saved = memory.ToJson(space);
	// A game update puts a new fighter first: F10 is now number 11.
	picks::PickSpace moved = space;
	picks::Fighter added = space.fighters.front();
	added.code = "NEW";
	moved.fighters.insert(moved.fighters.begin(), added);
	const PickMemory read = PickMemory::FromJson(saved, moved);
	CHECK(read.ChosenFighter() == 11);
	REQUIRE(read.LookOf(11).has_value());
	CHECK(SameLook(*read.LookOf(11), LookOf(11, 3, 4)));
	CHECK_FALSE(read.LookOf(10).has_value());
}

TEST_CASE("a saved look the pick space does not hold is left out: that fighter is in its default look")
{
	const picks::PickSpace space = LongSpace();
	const nlohmann::json zero = {{"costume", 0}, {"colour", 0}, {"vSkill", 0}, {"vTrigger", 0}};
	const auto saved = [](const char* code, nlohmann::json entry) {
		return nlohmann::json{{"version", 1}, {"looks", {{code, std::move(entry)}}}};
	};
	const auto holds = [&space](const nlohmann::json& file, int fighter) {
		return PickMemory::FromJson(file, space).LookOf(fighter).has_value();
	};
	// The look as saved is read, for the cases below to differ from it.
	CHECK(holds(saved("F05", {{"costume", 3}, {"colour", 5}, {"vSkill", 1}, {"vTrigger", 0}}), 5));
	// A costume the fighter lacks, a colour the costume lacks, an unshipped costume.
	CHECK_FALSE(holds(saved("F05", {{"costume", 1}, {"colour", 0}, {"vSkill", 0}, {"vTrigger", 0}}), 5));
	CHECK_FALSE(holds(saved("F05", {{"costume", 0}, {"colour", 3}, {"vSkill", 0}, {"vTrigger", 0}}), 5));
	CHECK_FALSE(holds(saved("F07", {{"costume", 0}, {"colour", 0}, {"vSkill", 0}, {"vTrigger", 0}}), 7));
	// A V-Trigger II for a fighter with one, a loadout that is no choice.
	CHECK_FALSE(holds(saved("F05", {{"costume", 0}, {"colour", 0}, {"vSkill", 0}, {"vTrigger", 1}}), 5));
	CHECK_FALSE(holds(saved("F05", {{"costume", 0}, {"colour", 0}, {"vSkill", 2}, {"vTrigger", 0}}), 5));
	CHECK_FALSE(holds(saved("F05", {{"costume", 0}, {"colour", 0}, {"vSkill", -1}, {"vTrigger", 0}}), 5));
	// A number that is no integer, a missing part, an entry that is no object.
	CHECK_FALSE(holds(saved("F05", {{"costume", "0"}, {"colour", 0}, {"vSkill", 0}, {"vTrigger", 0}}), 5));
	CHECK_FALSE(holds(saved("F05", {{"costume", 0.5}, {"colour", 0}, {"vSkill", 0}, {"vTrigger", 0}}), 5));
	CHECK_FALSE(holds(saved("F05", {{"costume", 0}, {"colour", 0}, {"vSkill", 0}}), 5));
	CHECK_FALSE(holds(saved("F05", 7), 5));
	// A code the space lacks leaves the others as they are.
	nlohmann::json two = saved("F99", zero);
	two["looks"]["F05"] = zero;
	const PickMemory read = PickMemory::FromJson(two, space);
	CHECK(read.LookOf(5).has_value());
	CHECK(std::count_if(space.fighters.begin(), space.fighters.end(),
			  [&read, &space](const picks::Fighter& fighter) { return read.LookOf((int)(&fighter - space.fighters.data())).has_value(); }) == 1);
	// A fighter chosen that the space lacks is none.
	CHECK_FALSE(PickMemory::FromJson({{"version", 1}, {"fighter", "F99"}}, space).ChosenFighter().has_value());
	CHECK_FALSE(PickMemory::FromJson({{"version", 1}, {"fighter", 5}}, space).ChosenFighter().has_value());
	CHECK(PickMemory::FromJson({{"version", 1}, {"fighter", "F05"}}, space).ChosenFighter() == 5);
	// Anything that is not the saved form is an empty memory.
	for (const nlohmann::json& other : {nlohmann::json(), nlohmann::json(3), nlohmann::json::array(),
			 nlohmann::json{{"looks", {{"F05", zero}}}}, nlohmann::json{{"version", 2}, {"looks", {{"F05", zero}}}},
			 nlohmann::json{{"version", 1}, {"looks", 4}}}) {
		const PickMemory empty = PickMemory::FromJson(other, space);
		CHECK_FALSE(empty.LookOf(5).has_value());
		CHECK_FALSE(empty.ChosenFighter().has_value());
	}
}

TEST_CASE("a fighter not stored yet starts in its default look: first costume, first colour, V-Skill I, V-Trigger I")
{
	const picks::PickSpace space = LongSpace();
	Pick look = picks::UnchosenLook(7);
	CHECK(picks::FitPick(space, look));
	// Fighter 7's first costume is unshipped: its first shipped one is used.
	CHECK(look.fighter == 7);
	CHECK(look.costume == 3);
	CHECK(look.color == 0);
	CHECK(picks::LoadoutOf(look) == kOneOne);
	Pick plain = picks::UnchosenLook(12);
	picks::FitPick(space, plain);
	CHECK(plain.costume == 0);
	CHECK(plain.color == 0);
}

TEST_CASE("choosing a fighter shows its own look, as the game's select does, not the one just left")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	// RYU in Nostalgia, colour 02, both II; CNL in colour 02, V-Skill II.
	PlayerPick ryu;
	ryu.pick = LookOf(kRyu, 4, 1, kTwoTwo);
	ryu = SetPick(ryu);
	PlayerPick cnl;
	cnl.pick = LookOf(kCnl, 0, 1, kTwoOne);
	cnl = SetPick(cnl);
	// Back to RYU: as left.
	CHECK(SameLook(LookFor(cnl.pick, kRyu, true), ryu.pick));
	// BSN was never stored: its default look, whatever CNL wears.
	const Pick bison = LookFor(cnl.pick, kBsn, true);
	CHECK(bison.fighter == kBsn);
	CHECK(bison.costume == 0);
	CHECK(bison.color == 0);
	CHECK(picks::LoadoutOf(bison) == kOneOne);
	// Stored, it is remembered in turn.
	PlayerPick stored;
	stored.pick = LookOf(kBsn, 0, 1, kTwoOne);
	SetPick(stored);
	CHECK(SameLook(LookFor(cnl.pick, kBsn, true), stored.pick));
	CHECK(SameLook(LookFor(cnl.pick, kRyu, true), ryu.pick));
	CHECK(SameLook(LookFor(cnl.pick, kCnl, true), cnl.pick));
	// The default look keeps the model size the pick has.
	PlayerPick tiny = cnl;
	tiny.pick.size = picks::kSizeTiny;
	ForgetPickMemoryForTest();
	CHECK(LookFor(tiny.pick, kRyu, true).size == picks::kSizeTiny);
	ForgetPickMemoryForTest();
}

TEST_CASE("a remembered look this player cannot wear falls back to the default look, not the nearest")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	// RYU's EX colour 100 of Nostalgia is the game's NOT OWNED here; costume 1
	// is theirs in no colour. Both are valid picks of the space, so a file
	// may hold them.
	PickMemory saved;
	saved.RememberLook(LookOf(kRyu, 4, 100, kTwoTwo));
	saved.Choose(kRyu);
	REQUIRE(RestorePickMemory(ThroughFile(saved, FilledPickSpace())));
	// The stored pick is RYU in the default look: costume 0, colour 0, both I.
	const PlayerPick pick = CurrentPick();
	CHECK(pick.pick.fighter == kRyu);
	CHECK(pick.pick.costume == 0);
	CHECK(pick.pick.color == 0);
	CHECK(picks::LoadoutOf(pick.pick) == kOneOne);
	// And so it is when RYU is chosen.
	const Pick chosen = LookFor(LookOf(kCnl, 0, 1, kTwoOne), kRyu, true);
	CHECK(chosen.costume == 0);
	CHECK(chosen.color == 0);
	CHECK(picks::LoadoutOf(chosen) == kOneOne);
	ForgetPickMemoryForTest();
}

TEST_CASE("the saved picks are restored once, before the player has picked, as the fighter last chosen in its look")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	PickMemory saved;
	saved.RememberLook(LookOf(kCnl, 0, 1, kTwoOne));
	saved.RememberLook(LookOf(kRyu, 4, 1, kTwoTwo));
	saved.Choose(kCnl);
	REQUIRE(RestorePickMemory(ThroughFile(saved, FilledPickSpace())));
	CHECK(SameLook(CurrentPick().pick, LookOf(kCnl, 0, 1, kTwoOne)));
	CHECK(SameLook(*RememberedLook(kRyu), LookOf(kRyu, 4, 1, kTwoTwo)));
	// Restoring is not a change to save.
	std::uint64_t revision = 99;
	PickMemoryNow(revision);
	CHECK(revision == 0);
	// A pick made since: the player's own choice wins over a late restore.
	PlayerPick ryu;
	ryu.pick = LookOf(kRyu, 0, 0, kOneOne);
	SetPick(ryu);
	CHECK_FALSE(RestorePickMemory(saved));
	CHECK(CurrentPick().pick.fighter == kRyu);
	ForgetPickMemoryForTest();
}

TEST_CASE("a late restore merges the saved looks under this run's: its own choices win per fighter, the rest come back")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	PickMemory saved;
	saved.RememberLook(LookOf(kCnl, 0, 1, kTwoOne));
	saved.RememberLook(LookOf(kRyu, 4, 1, kTwoTwo));
	saved.Choose(kCnl);
	// This run stored RYU in another look before the saved picks were read.
	PlayerPick ryu;
	ryu.pick = LookOf(kRyu, 0, 0, kOneOne);
	SetPick(ryu);
	CHECK_FALSE(RestorePickMemory(ThroughFile(saved, FilledPickSpace())));
	// RYU keeps this run's look and stays the fighter chosen; CNL's saved look is back.
	CHECK(SameLook(*RememberedLook(kRyu), LookOf(kRyu, 0, 0, kOneOne)));
	REQUIRE(RememberedLook(kCnl).has_value());
	CHECK(SameLook(*RememberedLook(kCnl), LookOf(kCnl, 0, 1, kTwoOne)));
	CHECK(CurrentPick().pick.fighter == kRyu);
	std::uint64_t revision = 0;
	CHECK(PickMemoryNow(revision).ChosenFighter() == kRyu);
	ForgetPickMemoryForTest();
	// A run that only rolled Random chose no fighter: the saved one is kept.
	SetPickRandom(true);
	PlayerPick rolled;
	rolled.pick = LookOf(kBsn, 0, 0, kOneOne);
	SetPick(rolled);
	CHECK_FALSE(RestorePickMemory(ThroughFile(saved, FilledPickSpace())));
	CHECK(PickMemoryNow(revision).ChosenFighter() == kCnl);
	CHECK(SameLook(*RememberedLook(kRyu), LookOf(kRyu, 4, 1, kTwoTwo)));
	SetPickRandom(false);
	ForgetPickMemoryForTest();
}

TEST_CASE("the picks are written only once they were read, never after a read that gave up, and once more after a late merge")
{
	// restored, readFailed, dirty, revision, saved
	CHECK_FALSE(PicksNeedSaving(false, false, false, 3, 0)); // not read yet: nothing is written
	CHECK_FALSE(PicksNeedSaving(true, false, false, 3, 3));  // nothing changed
	CHECK(PicksNeedSaving(true, false, false, 4, 3));        // a change
	CHECK(PicksNeedSaving(true, false, true, 3, 3));         // a late merge: the file lacks this run's looks
	CHECK_FALSE(PicksNeedSaving(true, true, false, 4, 0));   // the read never succeeded: never written
	CHECK_FALSE(PicksNeedSaving(true, true, true, 4, 0));
}

TEST_CASE("the Training dummy remembers a look for each fighter of its own, apart from the player's, and saves them with the picks")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	ForgetDummyForTest();
	PlayerPick ryu;
	ryu.pick = LookOf(kRyu, 4, 1, kTwoTwo);
	SetPick(ryu);
	std::uint64_t before = 0;
	PickMemoryNow(before);
	// The dummy has CNL in one look, then is moved to RYU through the same
	// path as the player's pick: RYU, never the dummy's, is in its default
	// look, not CNL's and not the player's.
	PlayerPick dummy;
	dummy.pick = LookOf(kCnl, 0, 1, kTwoOne);
	SetDummy(dummy);
	const PlayerPick toRyu = ChooseFighter(kRyu, {}, Whose::Dummy);
	CHECK(toRyu.pick.fighter == kRyu);
	CHECK(toRyu.pick.costume == 0);
	CHECK(toRyu.pick.color == 0);
	CHECK(picks::LoadoutOf(toRyu.pick) == kOneOne);
	CHECK(CurrentDummy().pick.fighter == kRyu);
	// Back to CNL, in the look the dummy last had it in.
	CHECK(SameLook(ChooseFighter(kCnl, {}, Whose::Dummy).pick, LookOf(kCnl, 0, 1, kTwoOne)));
	// The player's pick and memory are as they were.
	CHECK(CurrentPick().pick.fighter == kRyu);
	CHECK(SameLook(*RememberedLook(kRyu), LookOf(kRyu, 4, 1, kTwoTwo)));
	CHECK_FALSE(RememberedLook(kCnl).has_value());
	CHECK(SameLook(*RememberedLook(kCnl, Whose::Dummy), LookOf(kCnl, 0, 1, kTwoOne)));
	// What the dummy remembers is saved (the revision rose), under its own key,
	// and does not become the fighter last chosen.
	std::uint64_t after = 0;
	const PickMemory memory = PickMemoryNow(after);
	CHECK(after > before);
	const PickMemory read = ThroughFile(memory, FilledPickSpace());
	REQUIRE(read.LookOf(kCnl, Whose::Dummy).has_value());
	CHECK(SameLook(*read.LookOf(kCnl, Whose::Dummy), LookOf(kCnl, 0, 1, kTwoOne)));
	CHECK_FALSE(read.LookOf(kCnl).has_value());
	CHECK(read.ChosenFighter() == kRyu);
	// A file with no dummy key (beta.9's and the player's alone) reads as no dummy looks.
	PickMemory playerOnly;
	playerOnly.RememberLook(LookOf(kRyu, 0, 0, kOneOne));
	CHECK_FALSE(playerOnly.ToJson(FilledPickSpace()).contains("dummy"));
	// A late restore merges the saved dummy looks under this run's too.
	PickMemory saved;
	saved.RememberLook(LookOf(kCnl, 0, 0, kOneOne), Whose::Dummy);
	saved.RememberLook(LookOf(kBsn, 0, 0, kOneOne), Whose::Dummy);
	CHECK_FALSE(RestorePickMemory(ThroughFile(saved, FilledPickSpace())));
	CHECK(SameLook(*RememberedLook(kCnl, Whose::Dummy), LookOf(kCnl, 0, 1, kTwoOne)));
	CHECK(RememberedLook(kBsn, Whose::Dummy).has_value());
	ForgetDummyForTest();
	ForgetPickMemoryForTest();
}

TEST_CASE("only what is saved bumps the revision, and Random's rolls and Eleven's looks are not the player's choice")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	SetPickRandom(false);
	std::uint64_t revision = 0;
	PickMemoryNow(revision);
	CHECK(revision == 0);
	PlayerPick ryu;
	ryu.pick = LookOf(kRyu, 4, 1, kTwoTwo);
	SetPick(ryu);
	std::uint64_t after = 0;
	PickMemoryNow(after);
	CHECK(after > revision);
	// The same pick again: nothing to save.
	SetPick(ryu);
	std::uint64_t again = 0;
	PickMemoryNow(again);
	CHECK(again == after);
	// A fighter Random rolled has a look remembered but is not the fighter chosen.
	SetPickRandom(true);
	PlayerPick rolled;
	rolled.pick = LookOf(kCnl, 0, 1, kTwoOne);
	SetPick(rolled);
	std::uint64_t last = 0;
	const PickMemory memory = PickMemoryNow(last);
	CHECK(memory.ChosenFighter() == kRyu);
	CHECK(memory.LookOf(kCnl).has_value());
	// Eleven's look belongs to Eleven: neither remembered nor chosen.
	SetPickEleven(true);
	PlayerPick eleven;
	eleven.pick = LookOf(kBsn, 0, 0, kOneOne);
	SetPick(eleven);
	const PickMemory withEleven = PickMemoryNow(last);
	CHECK(withEleven.ChosenFighter() == kRyu);
	CHECK_FALSE(withEleven.LookOf(kBsn).has_value());
	SetPickEleven(false);
	SetPickRandom(false);
	ForgetPickMemoryForTest();
}

TEST_CASE("what is made from the stored pick is remade only when the pick, its looks or Random change, never by a read")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	SetPickRandom(false);
	PlayerPick ryu;
	ryu.pick = LookOf(kRyu, 4, 1, kTwoTwo);
	SetPick(ryu);
	const std::uint64_t base = PickChanges();
	// Reads change nothing, however many.
	for (int i = 0; i < 50; i++) {
		CurrentPick();
		CurrentDummy();
		RememberedLook(kRyu);
		LookFor(ryu.pick, kCnl, true);
		PickRandom();
		PickEleven();
	}
	CHECK(PickChanges() == base);
	// Each way to change it rises the count.
	std::uint64_t last = base;
	const auto rose = [&last] {
		const std::uint64_t now = PickChanges();
		const bool up = now > last;
		last = now;
		return up;
	};
	SetPick(ryu);
	CHECK(rose());
	SetPickRandom(true);
	CHECK(rose());
	SetPickEleven(true);
	CHECK(rose());
	SetPickEleven(false);
	SetPickRandom(false);
	rose();
	UpdatePick([](PlayerPick& pick) {
		pick.pick.color = 2;
		return true;
	});
	CHECK(rose());
	UpdatePick([](PlayerPick&) { return false; });
	CHECK_FALSE(rose());
	ForgetPickMemoryForTest();
	CHECK(rose());
}

TEST_CASE("the note to the runtime is due the first time and when what it is made from changes")
{
	RandomNoteGate gate;
	RandomNoteInputs inputs;
	inputs.fighters = 5;
	inputs.pick = 3;
	CHECK(gate.Due(inputs));
	for (int frame = 0; frame < 100; frame++) CHECK_FALSE(gate.Due(inputs));
	// Each input on its own.
	const auto changes = [&](auto change) {
		RandomNoteInputs other = inputs;
		change(other);
		const bool due = gate.Due(other);
		CHECK_FALSE(gate.Due(other));
		inputs = other;
		return due;
	};
	CHECK(changes([](RandomNoteInputs& i) { i.random = true; }));
	CHECK(changes([](RandomNoteInputs& i) { i.eleven = true; }));
	CHECK(changes([](RandomNoteInputs& i) { i.tiny = true; }));
	CHECK(changes([](RandomNoteInputs& i) { i.fighters = 6; }));
	CHECK(changes([](RandomNoteInputs& i) { i.pick++; }));
	CHECK(changes([](RandomNoteInputs& i) { i.voices++; }));
	CHECK(changes([](RandomNoteInputs& i) { i.roll++; }));
}

TEST_CASE("the own voices' revision rises only when a read gives other voices than the last")
{
	FillTestCatalog();
	const auto english = [](const std::string&) { return std::optional<int>(picks::kVoiceEnglish); };
	const auto japanese = [](const std::string&) { return std::optional<int>(picks::kVoiceJapanese); };
	RefreshOwnVoices(english);
	const std::uint64_t base = OwnVoicesRevision();
	RefreshOwnVoices(english);
	CHECK(OwnVoicesRevision() == base);
	RefreshOwnVoices(japanese);
	CHECK(OwnVoicesRevision() > base);
	RefreshOwnVoices(english); // as the other tests expect to find them
}
