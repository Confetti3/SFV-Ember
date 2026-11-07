#include "ember/SfvPickSpace.hxx"

#include "common/GameCatalog.hxx"

#include <nlohmann/json.hpp>

// Header-only: the room host's Linux build links no xxHash library.
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cstdio>

namespace sfve::picks {

std::string Problem(const PickSpace& space)
{
	if (space.fighters.empty() || space.stages.empty()) return "a list is empty";
	if (space.fighters.size() > (size_t)ember::MaxFighters) return "more fighters than a room carries";
	for (const Fighter& fighter : space.fighters) {
		if (fighter.costumes.empty()) return fighter.code + " has no costume";
		for (const Costume& costume : fighter.costumes) {
			if (costume.colours.empty()) return fighter.code + " costume " + std::to_string(costume.id) + " has no colour";
			if (costume.id < 0 || costume.id > 0xFF) return fighter.code + " has a costume number past a byte";
			for (int colour : costume.colours) {
				if (colour < 0 || colour > 0xFF) return fighter.code + " has a colour number past a byte";
			}
		}
	}
	return {};
}

std::string Fingerprint(const PickSpace& space)
{
	// Every list by its length and every code by its length, so no two pick
	// spaces read alike: the fighters, each with its costumes' numbers and
	// each costume's colours; then the stages.
	std::string text = std::to_string(space.fighters.size()) + ";";
	for (const Fighter& fighter : space.fighters) {
		text += std::to_string(fighter.code.size()) + ":" + fighter.code + std::to_string(fighter.costumes.size()) + "(";
		for (const Costume& costume : fighter.costumes) {
			text += std::to_string(costume.id) + "[";
			for (int colour : costume.colours) text += std::to_string(colour) + ",";
			text += "]";
		}
		text += ")";
	}
	text += std::to_string(space.stages.size()) + ";";
	for (const std::string& code : space.stages) text += std::to_string(code.size()) + ":" + code;
	char hex[17];
	std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)XXH3_64bits(text.data(), text.size()));
	return hex;
}

std::string BuildId(const PickSpace& space)
{
	return std::string(kMatchBuild) + "/" + Fingerprint(space);
}

const Fighter* FighterAt(const PickSpace& space, std::int64_t fighter)
{
	return fighter >= 0 && fighter < (std::int64_t)space.fighters.size() ? &space.fighters[(size_t)fighter] : nullptr;
}

const Costume* CostumeAt(const PickSpace& space, std::int64_t fighter, int costume)
{
	const Fighter* content = FighterAt(space, fighter);
	if (!content) return nullptr;
	for (const Costume& entry : content->costumes) {
		if (entry.id == costume) return &entry;
	}
	return nullptr;
}

bool HasColour(const Costume& costume, int colour)
{
	return std::find(costume.colours.begin(), costume.colours.end(), colour) != costume.colours.end();
}

bool ValidPick(const PickSpace& space, const sf4e::selection::Pick& pick)
{
	const Costume* costume = CostumeAt(space, pick.fighter, pick.costume);
	return costume && HasColour(*costume, pick.color) && pick.ultra == 0 && pick.handicap == 0;
}

bool ValidStage(const PickSpace& space, std::int64_t stage)
{
	return stage >= 0 && stage < (std::int64_t)space.stages.size();
}

bool FitPick(const PickSpace& space, sf4e::selection::Pick& pick)
{
	const sf4e::selection::Pick before = pick;
	if (!FighterAt(space, pick.fighter)) pick.fighter = 0;
	const Fighter* fighter = FighterAt(space, pick.fighter);
	const Costume* costume = CostumeAt(space, pick.fighter, pick.costume);
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

int CostumeLimit(const PickSpace& space)
{
	int limit = 0;
	for (const Fighter& fighter : space.fighters) {
		for (const Costume& costume : fighter.costumes) limit = std::max(limit, costume.id + 1);
	}
	return limit;
}

int ColourLimit(const PickSpace& space)
{
	int limit = 0;
	for (const Fighter& fighter : space.fighters) {
		for (const Costume& costume : fighter.costumes) {
			for (int colour : costume.colours) limit = std::max(limit, colour + 1);
		}
	}
	return limit;
}

int CostumeCount(const PickSpace& space, int fighter)
{
	int count = 0;
	if (const Fighter* content = FighterAt(space, fighter)) {
		for (const Costume& costume : content->costumes) count = std::max(count, costume.id + 1);
	}
	return count;
}

std::string ToJson(const PickSpace& space, const std::string& build)
{
	nlohmann::json fighters = nlohmann::json::array();
	for (const Fighter& fighter : space.fighters) {
		nlohmann::json costumes = nlohmann::json::array();
		for (const Costume& costume : fighter.costumes) costumes.push_back({{"id", costume.id}, {"colours", costume.colours}});
		fighters.push_back({{"code", fighter.code}, {"costumes", std::move(costumes)}});
	}
	return nlohmann::json{{"build", build}, {"fighters", std::move(fighters)}, {"stages", space.stages}}.dump(1, '\t') + "\n";
}

bool FromJson(const std::string& text, PickSpace& space, std::string& build, std::string& error)
{
	// Only what ToJson writes: an object with exactly these keys, codes as
	// strings and numbers as integers.
	const auto exactly = [](const nlohmann::json& value, std::initializer_list<const char*> keys) {
		if (!value.is_object() || value.size() != keys.size()) return false;
		for (const char* key : keys) {
			if (!value.contains(key)) return false;
		}
		return true;
	};
	const auto integer = [](const nlohmann::json& value) {
		return value.is_number_integer() && value.get<long long>() >= 0 && value.get<long long>() <= 0xFFFF;
	};
	PickSpace parsed;
	try {
		const nlohmann::json root = nlohmann::json::parse(text);
		if (!exactly(root, {"build", "fighters", "stages"}) || !root["build"].is_string() || !root["fighters"].is_array() ||
			!root["stages"].is_array()) {
			error = "not a pick space";
			return false;
		}
		for (const nlohmann::json& fighter : root["fighters"]) {
			if (!exactly(fighter, {"code", "costumes"}) || !fighter["code"].is_string() || !fighter["costumes"].is_array()) {
				error = "a fighter is not a code and costumes";
				return false;
			}
			Fighter entry{fighter["code"].get<std::string>(), {}};
			for (const nlohmann::json& costume : fighter["costumes"]) {
				if (!exactly(costume, {"id", "colours"}) || !integer(costume["id"]) || !costume["colours"].is_array()) {
					error = entry.code + ": a costume is not an id and colours";
					return false;
				}
				Costume numbers{costume["id"].get<int>(), {}};
				for (const nlohmann::json& colour : costume["colours"]) {
					if (!integer(colour)) {
						error = entry.code + ": a colour is not a number";
						return false;
					}
					numbers.colours.push_back(colour.get<int>());
				}
				entry.costumes.push_back(std::move(numbers));
			}
			parsed.fighters.push_back(std::move(entry));
		}
		for (const nlohmann::json& stage : root["stages"]) {
			if (!stage.is_string()) {
				error = "a stage is not a code";
				return false;
			}
			parsed.stages.push_back(stage.get<std::string>());
		}
		build = root["build"].get<std::string>();
	} catch (const nlohmann::json::exception& failure) {
		error = failure.what();
		return false;
	}
	space = std::move(parsed);
	return true;
}

} // namespace sfve::picks
