#include "ember/SfvPickSpace.hxx"

#include "common/GameCatalog.hxx"

#include <nlohmann/json.hpp>

// Header-only: the room host's Linux build links no xxHash library.
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cstdio>

namespace sfve::picks {

std::optional<Loadout> LoadoutOfNumbers(int vSkill, int vTrigger)
{
	const auto choice = [](int number) { return number >= 0 && number < kPartChoices; };
	if (!choice(vSkill) || !choice(vTrigger)) return std::nullopt;
	return Loadout{{(std::uint8_t)vSkill, (std::uint8_t)vTrigger}};
}

const char* Numeral(int number)
{
	return number == 0 ? "I" : number == 1 ? "II" : "";
}

int LoadoutIndex(Loadout loadout)
{
	return loadout.choice[VSkill] * kPartChoices + loadout.choice[VTrigger];
}

std::optional<Loadout> LoadoutAt(int index)
{
	if (index < 0 || index >= kLoadoutChoices) return std::nullopt;
	return LoadoutOfNumbers(index / kPartChoices, index % kPartChoices);
}

std::optional<Loadout> LoadoutIn(const sf4e::selection::Pick& pick)
{
	return LoadoutAt(pick.ultra);
}

Loadout LoadoutOf(const sf4e::selection::Pick& pick)
{
	return LoadoutIn(pick).value_or(Loadout{});
}

void SetLoadout(sf4e::selection::Pick& pick, Loadout loadout)
{
	pick.ultra = LoadoutIndex(loadout);
}

bool SameFitting(const sf4e::selection::Pick& a, const sf4e::selection::Pick& b)
{
	return a.fighter == b.fighter && a.costume == b.costume && a.color == b.color && a.ultra == b.ultra &&
		a.handicap == b.handicap && a.size == b.size;
}

std::optional<int> VoiceIn(const sf4e::selection::Pick& pick)
{
	const int voice = pick.winQuote - 1;
	if (voice == kVoiceJapanese || voice == kVoiceEnglish) return voice;
	return std::nullopt;
}

int VoiceOf(const sf4e::selection::Pick& pick)
{
	return VoiceIn(pick).value_or(kVoiceEnglish);
}

void SetVoice(sf4e::selection::Pick& pick, int voice)
{
	pick.winQuote = voice == kVoiceJapanese || voice == kVoiceEnglish ? voice + 1 : 0;
}

std::optional<int> LoadCodeIn(const sf4e::selection::Pick& pick)
{
	if (!(pick.personalAction & kLoadCodeMarker)) return std::nullopt;
	const int index = pick.personalAction & ~kLoadCodeMarker;
	if (index < 0 || index >= kLoadCodeCount) return std::nullopt;
	return index;
}

void SetLoadCode(sf4e::selection::Pick& pick, int index)
{
	pick.personalAction = kLoadCodeMarker | (index >= 0 && index < kLoadCodeCount ? index : 0);
}

std::uint32_t LoadCodeButtons(int index)
{
	return index >= 0 && index < kLoadCodeCount ? kLoadCodes[index].buttons : 0;
}

std::string LoadoutText(Loadout loadout)
{
	return std::string("vskill ") + Numeral(loadout.choice[VSkill]) + " vtrigger " + Numeral(loadout.choice[VTrigger]);
}

int PartChoices(const Fighter& fighter, LoadoutPart part)
{
	return part == LoadoutPart::VSkill ? fighter.vSkills : fighter.vTriggers;
}

bool HasLoadout(const Fighter& fighter, Loadout loadout)
{
	for (LoadoutPart part : kLoadoutParts) {
		if (loadout.choice[part] >= PartChoices(fighter, part)) return false;
	}
	return true;
}

Loadout FitLoadout(const Fighter& fighter, Loadout loadout)
{
	for (LoadoutPart part : kLoadoutParts) {
		if (loadout.choice[part] >= PartChoices(fighter, part)) loadout.choice[part] = 0;
	}
	return loadout;
}

std::vector<int> LoadoutIndices(const Fighter& fighter)
{
	std::vector<int> indices;
	for (int index = 0; index < kLoadoutChoices; index++) {
		if (HasLoadout(fighter, *LoadoutAt(index))) indices.push_back(index);
	}
	return indices;
}

std::string Problem(const PickSpace& space)
{
	if (space.fighters.empty() || space.stages.empty()) return "a list is empty";
	if (space.fighters.size() > (size_t)ember::MaxFighters) return "more fighters than a room carries";
	for (const Fighter& fighter : space.fighters) {
		for (LoadoutPart part : kLoadoutParts) {
			const int choices = PartChoices(fighter, part);
			if (choices < 1 || choices > kPartChoices) {
				return fighter.code + " has " + std::to_string(choices) + (part == LoadoutPart::VSkill ? " V-Skills" : " V-Triggers");
			}
		}
		if (fighter.costumes.empty()) return fighter.code + " has no costume";
		if (!FirstShipped(fighter)) return fighter.code + " has no costume it ships";
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

namespace {

// Every list by its length and every code by its length, so no two pick
// spaces read alike: the fighters, each with its costumes' numbers and each
// costume's colours, then its V-Skill and V-Trigger counts.
std::string FightersText(const PickSpace& space)
{
	std::string text = std::to_string(space.fighters.size()) + ";";
	for (const Fighter& fighter : space.fighters) {
		text += std::to_string(fighter.code.size()) + ":" + fighter.code + std::to_string(fighter.costumes.size()) + "(";
		for (const Costume& costume : fighter.costumes) {
			text += std::to_string(costume.id) + "[";
			for (int colour : costume.colours) text += std::to_string(colour) + ",";
			text += "]";
		}
		text += ")v" + std::to_string(fighter.vSkills) + "," + std::to_string(fighter.vTriggers) + ";";
	}
	return text;
}

// The stages, by the same rule.
std::string StagesText(const PickSpace& space)
{
	std::string text = std::to_string(space.stages.size()) + ";";
	for (const std::string& code : space.stages) text += std::to_string(code.size()) + ":" + code;
	return text;
}

std::string Hash(const std::string& text)
{
	char hex[17];
	std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)XXH3_64bits(text.data(), text.size()));
	return hex;
}

} // namespace

std::string Fingerprint(const PickSpace& space)
{
	return Hash(FightersText(space) + StagesText(space));
}

std::string BuildId(const PickSpace& space)
{
	return std::string(kMatchBuild) + "/" + Fingerprint(space);
}

std::string PartFingerprints(const PickSpace& space)
{
	return "picks " + Hash(FightersText(space)) + ", stages " + Hash(StagesText(space));
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
	const std::optional<Loadout> loadout = LoadoutIn(pick);
	return costume && !costume->unshipped && HasColour(*costume, pick.color) && loadout &&
		HasLoadout(*FighterAt(space, pick.fighter), *loadout) &&
		pick.handicap == 0 && (pick.size == kSizeNormal || pick.size == kSizeTiny);
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
	if ((!costume || costume->unshipped) && fighter) {
		// The first costume it ships; the first of all only for a fighter
		// with none, which Problem refuses.
		costume = FirstShipped(*fighter);
		if (!costume) costume = &fighter->costumes.front();
		pick.costume = costume->id;
	}
	if (costume && !HasColour(*costume, pick.color)) pick.color = costume->colours.front();
	SetLoadout(pick, fighter ? FitLoadout(*fighter, LoadoutOf(pick)) : Loadout{});
	pick.handicap = 0;
	if (pick.size != kSizeNormal && pick.size != kSizeTiny) pick.size = kSizeNormal;
	return !SameFitting(pick, before);
}

int CostumeLimit(const PickSpace& space)
{
	int limit = 0;
	for (const Fighter& fighter : space.fighters) {
		for (const Costume& costume : fighter.costumes) {
			if (!costume.unshipped) limit = std::max(limit, costume.id + 1);
		}
	}
	return limit;
}

int ColourLimit(const PickSpace& space)
{
	int limit = 0;
	for (const Fighter& fighter : space.fighters) {
		for (const Costume& costume : fighter.costumes) {
			if (costume.unshipped) continue;
			for (int colour : costume.colours) limit = std::max(limit, colour + 1);
		}
	}
	return limit;
}

int CostumeCount(const PickSpace& space, int fighter)
{
	int count = 0;
	if (const Fighter* content = FighterAt(space, fighter)) {
		for (const Costume& costume : content->costumes) {
			if (!costume.unshipped) count = std::max(count, costume.id + 1);
		}
	}
	return count;
}

const Costume* FirstShipped(const Fighter& fighter)
{
	for (const Costume& costume : fighter.costumes) {
		if (!costume.unshipped) return &costume;
	}
	return nullptr;
}

std::string ToJson(const PickSpace& space, const std::string& build)
{
	nlohmann::json fighters = nlohmann::json::array();
	for (const Fighter& fighter : space.fighters) {
		nlohmann::json costumes = nlohmann::json::array();
		for (const Costume& costume : fighter.costumes) {
			nlohmann::json entry = {{"id", costume.id}, {"colours", costume.colours}};
			if (costume.unshipped) entry["unshipped"] = true;
			costumes.push_back(std::move(entry));
		}
		fighters.push_back({{"code", fighter.code}, {"costumes", std::move(costumes)}, {"v_skills", fighter.vSkills},
			{"v_triggers", fighter.vTriggers}});
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
			if (!exactly(fighter, {"code", "costumes", "v_skills", "v_triggers"}) || !fighter["code"].is_string() ||
				!fighter["costumes"].is_array() || !integer(fighter["v_skills"]) || !integer(fighter["v_triggers"])) {
				error = "a fighter is not a code, costumes, and V-Skill and V-Trigger counts";
				return false;
			}
			Fighter entry{fighter["code"].get<std::string>(), {}, fighter["v_skills"].get<int>(), fighter["v_triggers"].get<int>()};
			for (const nlohmann::json& costume : fighter["costumes"]) {
				// "unshipped" is written only as true; the key alone is not.
				const bool marked = costume.is_object() && costume.contains("unshipped");
				if (!(marked ? exactly(costume, {"id", "colours", "unshipped"}) : exactly(costume, {"id", "colours"})) ||
					!integer(costume["id"]) || !costume["colours"].is_array() ||
					(marked && !(costume["unshipped"].is_boolean() && costume["unshipped"].get<bool>()))) {
					error = entry.code + ": a costume is not an id and colours (and \"unshipped\": true)";
					return false;
				}
				Costume numbers{costume["id"].get<int>(), {}, marked};
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
