#include "ember/SfvPickMemory.hxx"

#include <string>

namespace sfve::picks {

namespace {

constexpr int kVersion = 1;

bool Stored(int fighter)
{
	return fighter >= 0 && fighter < ::ember::MaxFighters;
}

// The saved look's number: an integer the key holds, or none.
std::optional<int> NumberAt(const nlohmann::json& object, const char* key)
{
	const auto found = object.find(key);
	if (found == object.end() || !found->is_number_integer()) return std::nullopt;
	const std::int64_t value = found->get<std::int64_t>();
	if (value < -1 || value > 0xFF) return std::nullopt;
	return (int)value;
}

// The pick a saved look names for `fighter`; none when it is not one the
// space holds for that fighter.
std::optional<sf4e::selection::Pick> LookRead(const nlohmann::json& saved, const PickSpace& space, int fighter)
{
	if (!saved.is_object()) return std::nullopt;
	const std::optional<int> costume = NumberAt(saved, "costume"), colour = NumberAt(saved, "colour");
	const std::optional<int> vSkill = NumberAt(saved, "vSkill"), vTrigger = NumberAt(saved, "vTrigger");
	if (!costume || !colour || !vSkill || !vTrigger) return std::nullopt;
	const std::optional<Loadout> loadout = LoadoutOfNumbers(*vSkill, *vTrigger);
	if (!loadout) return std::nullopt;
	sf4e::selection::Pick pick;
	pick.fighter = fighter;
	pick.costume = *costume;
	pick.color = *colour;
	SetLoadout(pick, *loadout);
	if (!ValidPick(space, pick)) return std::nullopt;
	return pick;
}

std::optional<int> FighterOfCode(const PickSpace& space, const std::string& code)
{
	for (size_t id = 0; id < space.fighters.size(); id++) {
		if (space.fighters[id].code == code) return (int)id;
	}
	return std::nullopt;
}

} // namespace

sf4e::selection::Pick UnchosenLook(int fighter)
{
	sf4e::selection::Pick pick;
	pick.fighter = fighter;
	pick.costume = -1;
	pick.color = -1;
	return pick;
}

std::optional<sf4e::selection::Pick> PickMemory::LookOf(int fighter, Whose whose) const
{
	return Stored(fighter) ? LooksOf(whose)[(size_t)fighter] : std::nullopt;
}

bool PickMemory::RememberLook(const sf4e::selection::Pick& pick, Whose whose)
{
	if (!Stored(pick.fighter)) return false;
	std::optional<sf4e::selection::Pick>& look = LooksOf(whose)[(size_t)pick.fighter];
	const bool changed = !look || look->costume != pick.costume || look->color != pick.color || look->ultra != pick.ultra;
	look = pick;
	return changed;
}

bool PickMemory::MergeUnder(const PickMemory& saved)
{
	bool taken = false;
	for (size_t id = 0; id < looks_.size(); id++) {
		if (!looks_[id] && saved.looks_[id]) {
			looks_[id] = saved.looks_[id];
			taken = true;
		}
		if (!dummyLooks_[id] && saved.dummyLooks_[id]) {
			dummyLooks_[id] = saved.dummyLooks_[id];
			taken = true;
		}
	}
	if (!chosen_ && saved.chosen_) {
		chosen_ = saved.chosen_;
		taken = true;
	}
	return taken;
}

bool PickMemory::Choose(int fighter)
{
	if (!Stored(fighter) || chosen_ == fighter) return false;
	chosen_ = fighter;
	return true;
}

nlohmann::json PickMemory::ToJson(const PickSpace& space) const
{
	// A fighter's looks by code, in the space's order, for fighters it holds.
	const auto write = [&space](const Looks& looks) {
		nlohmann::json written = nlohmann::json::object();
		for (size_t id = 0; id < space.fighters.size() && id < looks.size(); id++) {
			if (!looks[id]) continue;
			const sf4e::selection::Pick& pick = *looks[id];
			const Loadout loadout = LoadoutOf(pick);
			written[space.fighters[id].code] = {{"costume", pick.costume}, {"colour", pick.color}, {"vSkill", loadout.choice[VSkill]},
				{"vTrigger", loadout.choice[VTrigger]}};
		}
		return written;
	};
	nlohmann::json saved = {{"version", kVersion}, {"looks", write(looks_)}};
	if (chosen_ && FighterAt(space, *chosen_)) saved["fighter"] = FighterAt(space, *chosen_)->code;
	nlohmann::json dummy = write(dummyLooks_);
	if (!dummy.empty()) saved["dummy"] = {{"looks", std::move(dummy)}};
	return saved;
}

PickMemory PickMemory::FromJson(const nlohmann::json& saved, const PickSpace& space)
{
	PickMemory memory;
	if (!saved.is_object()) return memory;
	const auto version = saved.find("version");
	if (version == saved.end() || !version->is_number_integer() || version->get<int>() != kVersion) return memory;
	// The looks of an object of fighters' codes, into `into`.
	const auto read = [&space](const nlohmann::json& looks, Looks& into) {
		if (!looks.is_object()) return;
		for (const auto& [code, entry] : looks.items()) {
			const std::optional<int> fighter = FighterOfCode(space, code);
			if (!fighter || !Stored(*fighter)) continue;
			if (const std::optional<sf4e::selection::Pick> pick = LookRead(entry, space, *fighter)) into[(size_t)*fighter] = *pick;
		}
	};
	const auto looks = saved.find("looks");
	if (looks != saved.end()) read(*looks, memory.looks_);
	const auto dummy = saved.find("dummy");
	if (dummy != saved.end() && dummy->is_object()) {
		const auto dummyLooks = dummy->find("looks");
		if (dummyLooks != dummy->end()) read(*dummyLooks, memory.dummyLooks_);
	}
	const auto chosen = saved.find("fighter");
	if (chosen != saved.end() && chosen->is_string()) {
		const std::optional<int> fighter = FighterOfCode(space, chosen->get<std::string>());
		if (fighter && Stored(*fighter)) memory.chosen_ = *fighter;
	}
	return memory;
}

} // namespace sfve::picks
