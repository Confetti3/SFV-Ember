#include "ember/SfvLoadoutText.hxx"

#include "ember/SfvCatalog.hxx"
#include "mod/GameContent.hxx"
#include "mod/Log.hxx"

#include <array>
#include <atomic>
#include <utility>

namespace sfve::adapter {

namespace {

// " · ", as the shared screens join a seat's parts (the library's room.seat_look).
constexpr const char* kJoin = " \xC2\xB7 ";
constexpr const char* kArgument = "{common1}";

// One language's words, with the shared screens' texts made from them.
struct Words {
	std::string vSkill = "V-Skill";
	std::string vTrigger = "V-Trigger";
	std::array<std::string, picks::kPartChoices> choices; // the game's patterns; empty for none
	std::array<std::string, picks::kLoadoutChoices> labels, shortLabels;
};

void BuildLabels(Words& words)
{
	for (int index = 0; index < picks::kLoadoutChoices; index++) {
		const picks::Loadout loadout = *picks::LoadoutAt(index);
		const char* skill = Numeral(picks::PartNumber(loadout, picks::LoadoutPart::VSkill));
		const char* trigger = Numeral(picks::PartNumber(loadout, picks::LoadoutPart::VTrigger));
		words.labels[(size_t)index] = words.vSkill + " " + skill + kJoin + words.vTrigger + " " + trigger;
		words.shortLabels[(size_t)index] = std::string(skill) + "/" + trigger;
	}
}

// English, until the game's words are published.
const Words& English()
{
	static const Words words = [] {
		Words english;
		BuildLabels(english);
		return english;
	}();
	return words;
}

// Written once, before `s_published` is set, and never again (ReadLoadoutWords).
Words s_words;
std::atomic<bool> s_published{false};

const Words& Current()
{
	return s_published.load(std::memory_order_acquire) ? s_words : English();
}

} // namespace

void ReadLoadoutWords()
{
	static bool read = false;
	if (read) return;
	read = true;
	Words words = English();
	const std::string vSkill = mod::LocalizedText(L"KW", L"ID_SYS_VSE_0002");
	const std::string vTrigger = mod::LocalizedText(L"KW", L"ID_SYS_VSE_0001");
	if (!vSkill.empty()) words.vSkill = vSkill;
	if (!vTrigger.empty()) words.vTrigger = vTrigger;
	words.choices[0] = mod::LocalizedText(L"KW", L"ID_SYS_VSE_0004");
	words.choices[1] = mod::LocalizedText(L"KW", L"ID_SYS_VSE_0005");
	BuildLabels(words);
	mod::Log("loadout: the game's words: ID_SYS_VSE_0002 \"%s\", _0001 \"%s\", _0004 \"%s\", _0005 \"%s\" (empty: none, "
		"English kept)", vSkill.c_str(), vTrigger.c_str(), words.choices[0].c_str(), words.choices[1].c_str());
	s_words = std::move(words);
	s_published.store(true, std::memory_order_release);
}

const char* PartLabel(picks::LoadoutPart part)
{
	return (part == picks::LoadoutPart::VSkill ? Current().vSkill : Current().vTrigger).c_str();
}

const char* Numeral(int number)
{
	return number == 0 ? "I" : number == 1 ? "II" : "";
}

std::string FormatChoice(const std::string& pattern, const char* numeral, const std::string& move)
{
	if (move.empty()) return numeral;
	const size_t argument = pattern.find(kArgument);
	if (argument != std::string::npos) return std::string(pattern).replace(argument, std::char_traits<char>::length(kArgument), move);
	return (pattern.empty() ? std::string(numeral) : pattern) + " " + move;
}

std::string ChoiceText(int number, const std::string& move)
{
	const auto& choices = Current().choices;
	const std::string& pattern = number >= 0 && number < (int)choices.size() ? choices[(size_t)number] : std::string();
	return FormatChoice(pattern, Numeral(number), move);
}

const char* LoadoutLabel(int index)
{
	return index >= 0 && index < picks::kLoadoutChoices ? Current().labels[(size_t)index].c_str() : "";
}

const char* ShortLoadoutLabel(int index)
{
	return index >= 0 && index < picks::kLoadoutChoices ? Current().shortLabels[(size_t)index].c_str() : "";
}

std::string LoadoutMoves(std::int64_t fighter, picks::Loadout loadout)
{
	std::string moves;
	for (picks::LoadoutPart part : picks::kLoadoutParts) {
		const int number = picks::PartNumber(loadout, part);
		const std::string name = MoveName(fighter, part, number);
		moves += (moves.empty() ? "" : kJoin) + (name.empty() ? std::string(PartLabel(part)) + " " + Numeral(number) : name);
	}
	return moves;
}

std::string LoadoutName(std::int64_t fighter, picks::Loadout loadout)
{
	const std::string label = LoadoutLabel(picks::LoadoutIndex(loadout));
	for (picks::LoadoutPart part : picks::kLoadoutParts) {
		if (!MoveName(fighter, part, picks::PartNumber(loadout, part)).empty()) return label + ": " + LoadoutMoves(fighter, loadout);
	}
	return label;
}

} // namespace sfve::adapter
