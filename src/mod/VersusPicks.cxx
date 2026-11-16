// What a pick is as text and as the game's rules have it: the launch
// command's and a room's setup words (ParseSetup, pick_text), the rounds and
// round times SFV plays, the voice a fighter is written with, and the
// CharPkg a pick fills. Pure but for FillCharPkg; VersusLaunch.cxx launches.

#include "mod/VersusLaunch.hxx"

#include "mod/GameContent.hxx"
#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/VersusPicks.hxx"

#include <stdlib.h>

#include <sstream>

namespace sfve::mod {

namespace {

namespace jump = sfv::jump;

constexpr size_t kMaxCode = 8;


// A costume or colour number, 0 to 255 (a room carries it in a byte), or
// "-"/nothing for the default.
bool ParseVariant(const std::string& text, int* value)
{
	if (text.empty() || text == "-") return *value = -1, true;
	if (text.size() > 3 || text.find_first_not_of("0123456789") != std::string::npos) return false;
	*value = atoi(text.c_str());
	return *value <= 0xFF;
}

// A V-Skill or V-Trigger as the game calls it, "1" for I or "2" for II, into
// its number; "-"/nothing for I.
bool ParsePart(const std::string& text, int* number)
{
	if (text.empty() || text == "-") return *number = 0, true;
	if (text != "1" && text != "2") return false;
	*number = text[0] - '1';
	return true;
}

bool GameVoice(int voice)
{
	return voice == jump::kVoiceJapanese || voice == jump::kVoiceEnglish;
}

} // namespace

namespace pick_text {

bool ParseCode(const std::string& text, std::wstring* code)
{
	if (text == "-") return code->clear(), true;
	if (text.empty() || text.size() > kMaxCode) return false;
	for (char c : text) {
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
	}
	code->assign(text.begin(), text.end());
	return true;
}

// A digit of PlayableRounds, or "-"/nothing for the default.
bool ParseRounds(const std::string& text, int* rounds)
{
	if (text.empty() || text == "-") return *rounds = -1, true;
	if (text.size() != 1 || !PlayableRounds(text[0] - '0')) return false;
	*rounds = text[0] - '0';
	return true;
}

// Seconds (RoundClockFrames) or "inf"; "-"/nothing for the default.
bool ParseTime(const std::string& text, int* frames)
{
	if (text.empty() || text == "-") return *frames = -1, true;
	if (text == "inf") return *frames = MatchRules::kNoTimeLimit, true;
	if (text.size() > 3 || text.find_first_not_of("0123456789") != std::string::npos) return false;
	const std::optional<int> clock = RoundClockFrames(atoi(text.c_str()));
	if (!clock) return false;
	*frames = *clock;
	return true;
}

// "CODE[:costume[:colour[:vskill[:vtrigger]]]]" or "-".
bool ParsePick(const std::string& word, FighterPick* pick)
{
	*pick = FighterPick{};
	if (word.empty() || word == "-") return true;
	std::istringstream in(word);
	std::string code, costume, colour, vSkill, vTrigger, extra;
	std::getline(in, code, ':');
	std::getline(in, costume, ':');
	std::getline(in, colour, ':');
	std::getline(in, vSkill, ':');
	std::getline(in, vTrigger, ':');
	return !std::getline(in, extra, ':') && ParseCode(code, &pick->fighter) && ParseVariant(costume, &pick->costume) &&
		ParseVariant(colour, &pick->colour) && ParsePart(vSkill, &pick->vSkill) && ParsePart(vTrigger, &pick->vTrigger);
}

} // namespace pick_text

using namespace pick_text;

int VoiceToWrite(const FighterPick& pick, std::optional<int> ofOptions)
{
	if (GameVoice(pick.voice)) return pick.voice;
	return ofOptions && GameVoice(*ofOptions) ? *ofOptions : jump::kVoiceEnglish;
}

void FillCharPkg(uint8_t* chara, const FighterPick& pick)
{
	if (pick.costume >= 0) Field<int32_t>(chara, jump::kCharCostume) = pick.costume;
	if (pick.colour >= 0) Field<int32_t>(chara, jump::kCharColour) = pick.colour;
	Field<int32_t>(chara, jump::kCharVSkill) = pick.vSkill;
	Field<int32_t>(chara, jump::kCharVTrigger) = pick.vTrigger;
	if (GameVoice(pick.voice)) chara[jump::kCharVoice] = (uint8_t)pick.voice;
}

std::string VoiceName(int voice)
{
	if (voice == jump::kVoiceJapanese) return "japanese";
	if (voice == jump::kVoiceEnglish) return "english";
	return std::to_string(voice);
}

bool LookMismatch(const PlayerLook& written, const PlayerLook& loaded)
{
	const bool voices = written.voice >= 0 && loaded.voice >= 0;
	return !SameCode(written.fighter, loaded.fighter) || written.costume != loaded.costume || written.colour != loaded.colour ||
		(voices && written.voice != loaded.voice);
}

bool PlayableRounds(int rounds)
{
	return rounds == 1 || rounds == 3 || rounds == 5 || rounds == 7;
}

std::optional<int> RoundClockFrames(int seconds)
{
	constexpr int kFramesPerSecond = 60, kMinSeconds = 30, kMaxSeconds = 32767 / kFramesPerSecond;
	if (seconds < kMinSeconds || seconds > kMaxSeconds) return std::nullopt;
	return seconds * kFramesPerSecond;
}

std::optional<int> RoomClockFrames(int seconds)
{
	if (seconds == kNoLimitSeconds) return MatchRules::kNoTimeLimit;
	return RoundClockFrames(seconds);
}

bool ParseSetup(const std::string& setup, FighterPick* fighter, MatchRules* rules)
{
	std::istringstream in(setup);
	std::string pick, stageCode, rounds, time, extra;
	in >> pick >> stageCode >> rounds >> time;
	*rules = MatchRules{};
	return !(in >> extra) && ParsePick(pick, fighter) && (stageCode.empty() || ParseCode(stageCode, &rules->stage)) &&
		ParseRounds(rounds, &rules->rounds) && ParseTime(time, &rules->timeFrames);
}

} // namespace sfve::mod
