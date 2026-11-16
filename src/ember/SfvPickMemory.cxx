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

std::optional<sf4e::selection::Pick> PickMemory::LookOf(int fighter) const
{
	return Stored(fighter) ? looks_[(size_t)fighter] : std::nullopt;
}

bool PickMemory::RememberLook(const sf4e::selection::Pick& pick)
{
	if (!Stored(pick.fighter)) return false;
	std::optional<sf4e::selection::Pick>& look = looks_[(size_t)pick.fighter];
	const bool changed = !look || look->costume != pick.costume || look->color != pick.color || look->ultra != pick.ultra;
	look = pick;
	return changed;
}

bool PickMemory::Choose(int fighter)
{
	if (!Stored(fighter) || chosen_ == fighter) return false;
	chosen_ = fighter;
	return true;
}

nlohmann::json PickMemory::ToJson(const PickSpace& space) const
{
	nlohmann::json looks = nlohmann::json::object();
	for (size_t id = 0; id < space.fighters.size() && id < looks_.size(); id++) {
		if (!looks_[id]) continue;
		const sf4e::selection::Pick& pick = *looks_[id];
		const Loadout loadout = LoadoutOf(pick);
		looks[space.fighters[id].code] = {{"costume", pick.costume}, {"colour", pick.color}, {"vSkill", loadout.choice[VSkill]},
			{"vTrigger", loadout.choice[VTrigger]}};
	}
	nlohmann::json saved = {{"version", kVersion}, {"looks", std::move(looks)}};
	if (chosen_ && FighterAt(space, *chosen_)) saved["fighter"] = FighterAt(space, *chosen_)->code;
	return saved;
}

PickMemory PickMemory::FromJson(const nlohmann::json& saved, const PickSpace& space)
{
	PickMemory memory;
	if (!saved.is_object()) return memory;
	const auto version = saved.find("version");
	if (version == saved.end() || !version->is_number_integer() || version->get<int>() != kVersion) return memory;
	const auto looks = saved.find("looks");
	if (looks != saved.end() && looks->is_object()) {
		for (const auto& [code, entry] : looks->items()) {
			const std::optional<int> fighter = FighterOfCode(space, code);
			if (!fighter || !Stored(*fighter)) continue;
			if (const std::optional<sf4e::selection::Pick> pick = LookRead(entry, space, *fighter)) memory.looks_[(size_t)*fighter] = *pick;
		}
	}
	const auto chosen = saved.find("fighter");
	if (chosen != saved.end() && chosen->is_string()) {
		const std::optional<int> fighter = FighterOfCode(space, chosen->get<std::string>());
		if (fighter && Stored(*fighter)) memory.chosen_ = *fighter;
	}
	return memory;
}

} // namespace sfve::picks
