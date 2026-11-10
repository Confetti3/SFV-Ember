#include "mod/VersusLaunch.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <array>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace sfve::mod {

namespace {

namespace jump = sfv::jump;

using GetClassFn = uint8_t* (*)(const wchar_t* package);
using NewObjectFn = uint8_t* (*)(uint8_t* outer, uint8_t* objectClass);
using JumpStepFn = void* (*)(uint8_t* jumpState);
using GetFlowFn = GameRef* (*)(GameRef* out);
using EnsureLoaderFn = void (*)(uint8_t* flow, int providerType);
using GameDataFn = uint8_t* (*)();
using PlayerRefFn = uint8_t* (*)(uint8_t* gameData, int player);
using GetPlayerSetupFn = void (*)(uint8_t* playerRef, uint8_t* setup);
using CopyFn = uint8_t* (*)(uint8_t* out, const uint8_t* from);
using AssignStringFn = void (*)(uint8_t* to, const GameString* from, int extra);
using FreeFn = void (*)(void* block);
using GameInstanceFn = uint8_t* (*)();

// A launch that has not reached the jump's second step by then has failed
// (the jump is normally there within a few frames).
constexpr int kLaunchFrames = 60 * 10;
// After the picks are written the game loads the stage and fighters (about
// 4 s); a load still not done after this long is reported as failed.
constexpr int kLoadFrames = 60 * 60;
// A launch waiting for the main menu gives up after this long.
constexpr int kWaitForMenuFrames = 60 * 120;
constexpr int kTargets = 3;
constexpr int kDataTypeForTarget[kTargets] = {jump::kVersusCpuType, jump::kVersusType, jump::kTrainingType};

uintptr_t s_base;
bool s_installed; // both jump steps hooked: without them the jump crashes from the menu
bool s_checkOwnership;
JumpStepFn o_jumpCopyPlayers;
JumpStepFn o_jumpSecondStep;

// Game thread only: the launch in progress, from Launch until the jump's
// second step has written its picks, or the launch failed.
struct Pending {
	uint8_t* message = nullptr; // the init message the mod made (the jump keeps it)
	bool loaderGiven = false;
	MatchPicks picks;
	LaunchFailed onFailed;
};
Pending s_pending;
// Game thread only: a linked launch's picks went into the jump and its
// battle has not bound yet (TakeLinkedBattle).
bool s_linkedLoading = false;
bool s_linkedPractice = false; // ...and it was a Practice room's
// The same for a replay's launch (TakeReplayBattle).
bool s_replayLoading = false;

template <typename Fn>
Fn At(const CodeSite& site)
{
	return reinterpret_cast<Fn>(s_base + site.rva);
}

template <typename R, typename... Args>
R Virtual(uint8_t* object, uint32_t offset, Args... args)
{
	using Fn = R (*)(uint8_t*, Args...);
	return (*reinterpret_cast<Fn*>(*reinterpret_cast<uintptr_t*>(object) + offset))(object, args...);
}

void FreeGameBlock(uint8_t* owner, uint32_t pointerOffset)
{
	if (void* block = Field<void*>(owner, pointerOffset)) At<FreeFn>(sfv::kGameFree)(block);
}

void AssignString(uint8_t* gameString, const std::wstring& text)
{
	const GameString from{text.c_str(), (int32_t)text.size() + 1, (int32_t)text.size() + 1};
	At<AssignStringFn>(sfv::kAssignString)(gameString, &from, 0);
}

std::string Narrow(const uint8_t* gameString)
{
	const GameString& s = *reinterpret_cast<const GameString*>(gameString);
	std::string out;
	for (int i = 0; s.data && i < s.count && s.data[i]; i++) out += (char)s.data[i];
	return out;
}

uint8_t* ModeData(uint8_t* flow)
{
	uint8_t* loader = Field<uint8_t*>(flow, jump::kFlowLoader);
	return loader ? Field<uint8_t*>(loader, jump::kLoaderProvider) : nullptr;
}

bool IsOurJump(uint8_t* jumpState)
{
	return s_pending.message && Field<uint8_t*>(jumpState, jump::kJumpStateMessage) == s_pending.message;
}

// Ends the launch in progress, reporting why to whoever asked for it.
void FailLaunch(const std::string& why)
{
	Log("launch: failed: %s", why.c_str());
	const LaunchFailed onFailed = std::move(s_pending.onFailed);
	s_pending = Pending{};
	if (onFailed) onFailed(why);
}

// The first step copies the players from the flow's loader into the battle
// setting. Without a loader (the main menu reset the flow) it would read
// through null; give it one holding the setting's own players, so the copy
// changes nothing. If even that cannot be had, the jump is sent back to the
// main menu (its own "-1" target), which that step checks first.
void* h_JumpCopyPlayers(uint8_t* jumpState)
{
	if (IsOurJump(jumpState) && !s_pending.loaderGiven) {
		s_pending.loaderGiven = true;
		GameRef flow{};
		At<GetFlowFn>(sfv::kGetBattleFlow)(&flow);
		bool haveLoader = flow.object && Field<uint8_t*>(flow.object, jump::kFlowLoader);
		if (flow.object && !haveLoader) {
			At<EnsureLoaderFn>(sfv::kEnsureLoader)(flow.object, jump::kProviderType);
			uint8_t* provider = ModeData(flow.object);
			uint8_t* gameData = At<GameDataFn>(sfv::kGameData)();
			haveLoader = provider && gameData;
			for (int player = 0; haveLoader && player < jump::kPlayers; player++) {
				uint8_t* ref = At<PlayerRefFn>(sfv::kPlayerRef)(gameData, player);
				At<GetPlayerSetupFn>(sfv::kGetPlayerSetup)(ref, provider + jump::kProviderPlayers + jump::kPlayerSetupSize * player);
			}
		}
		GiveBackGameRef(flow);
		if (!haveLoader) {
			Field<int32_t>(s_pending.message, jump::kMessageMode) = jump::kModeBackToMainMenu;
			FailLaunch("the battle flow has no loader to give the jump; back to the main menu");
		}
	}
	return o_jumpCopyPlayers(jumpState);
}

// Why the jump's mode data cannot take the picks, or empty when it can: it
// must be the target's type and hold a fighter record for each player, so
// every launch writes both players' fighter, V-Skill and V-Trigger, or fails
// before writing any.
std::string ModeDataRefusal(uint8_t* modeData, LaunchTarget target)
{
	const int wantedType = kDataTypeForTarget[(int)target];
	const int type = modeData ? Virtual<int>(modeData, jump::kDataType) : -1;
	if (type != wantedType) return "the jump's mode data is type " + std::to_string(type) + ", not " + std::to_string(wantedType);
	for (int player = 0; player < jump::kPlayers; player++) {
		if (!Virtual<const uint8_t*>(modeData, jump::kDataGetChara, player)) {
			return "player " + std::to_string(player + 1) + " has no fighter record in the jump's mode data";
		}
	}
	return {};
}

// Both players' looks (PlayerLook), as a launch wrote them into the jump.
using PlayerLooks = std::array<PlayerLook, jump::kPlayers>;

// The look the battle setting holds for player `player` (sfv::jump's
// kSettingCostume and the rest); none where the setting has no record for it.
std::optional<PlayerLook> LoadedLook(int player)
{
	uint8_t* gameData = At<GameDataFn>(sfv::kGameData)();
	uint8_t* ref = gameData ? At<PlayerRefFn>(sfv::kPlayerRef)(gameData, player) : nullptr;
	uint8_t* records = ref ? Field<uint8_t*>(ref, jump::kRefRecords) : nullptr;
	const uint32_t index = ref ? Field<uint32_t>(ref, jump::kRefRecord) : jump::kSettingRecords;
	if (!records || index >= jump::kSettingRecords) return std::nullopt;
	uint8_t* record = records + (size_t)jump::kSettingRecordSize * index;
	const char* code = reinterpret_cast<const char*>(record + jump::kSettingCode);
	return PlayerLook{std::string(code, strnlen(code, jump::kSettingCodeSize)), Field<int32_t>(record, jump::kSettingCostume),
		Field<int32_t>(record, jump::kSettingColour)};
}

// Once the battle has loaded, each player's look as the battle setting holds
// it, in the launch line's words, so two copies' logs can be compared line
// for line. The battle's load puts a costume whose package this copy lacks
// back to 0 (sfv::jump's kSettingCostume), and the game's ownership check,
// where it runs, may too; either shows only on screen. A look that is not
// the one the launch wrote is said as a warning: the other copy, whose load
// kept it, shows the players a different look.
void LogLoadedLooks(const PlayerLooks& written)
{
	for (int player = 0; player < jump::kPlayers; player++) {
		const std::optional<PlayerLook> loaded = LoadedLook(player);
		if (!loaded) {
			Log("launch: player %d's loaded look cannot be read from the battle setting", player + 1);
			continue;
		}
		Log("launch: player %d loaded %s costume %d colour %d", player + 1, loaded->fighter.c_str(), loaded->costume,
			loaded->colour);
		const PlayerLook& launched = written[(size_t)player];
		if (!LookMismatch(launched, *loaded)) continue;
		Log("launch: warning: player %d loaded %s costume %d colour %d, not the %s costume %d colour %d launched; another "
			"copy that loaded the launched look shows a different one",
			player + 1, loaded->fighter.c_str(), loaded->costume, loaded->colour, launched.fighter.c_str(), launched.costume,
			launched.colour);
	}
}

// Mode data ModeDataRefusal accepted. Returns what it wrote.
PlayerLook WriteFighter(uint8_t* modeData, int player, const FighterPick& pick)
{
	const uint8_t* current = Virtual<const uint8_t*>(modeData, jump::kDataGetChara, player);
	alignas(8) uint8_t chara[jump::kCharPkgSize];
	At<CopyFn>(sfv::kCopyCharPkg)(chara, current);
	const std::string before = Narrow(chara + jump::kCharCode);
	if (!pick.fighter.empty()) AssignString(chara + jump::kCharCode, pick.fighter);
	FillCharPkg(chara, pick);
	// The game's own order (rva 0x602C80, 0x6C8540): the fighter's setup,
	// then the selected fighter. Both copy the record.
	Virtual<void>(modeData, jump::kDataSetChara, player, (const uint8_t*)chara);
	Virtual<void>(modeData, jump::kDataSetCode, player, (const uint8_t*)chara);
	const PlayerLook written{Narrow(chara + jump::kCharCode), Field<int32_t>(chara, jump::kCharCostume),
		Field<int32_t>(chara, jump::kCharColour)};
	// The V-Skill and V-Trigger as the pipe names them, 1 for I and 2 for II.
	Log("launch: player %d %s -> %s costume %d colour %d vskill %d vtrigger %d", player + 1, before.c_str(),
		written.fighter.c_str(), written.costume, written.colour, Field<int32_t>(chara, jump::kCharVSkill) + 1,
		Field<int32_t>(chara, jump::kCharVTrigger) + 1);
	// What the copy allocated: the code's characters and the items' array
	// (its 16-byte items hold no pointers: the copy is a memcpy).
	FreeGameBlock(chara, jump::kCharCode);
	FreeGameBlock(chara, jump::kCharItems);
	return written;
}

void WriteStage(uint8_t* modeData, const MatchPicks& picks)
{
	const uint8_t* current = Virtual<const uint8_t*>(modeData, jump::kDataGetSpec);
	if (!current) return Log("launch: no match record; the stage is left as it is");
	alignas(8) uint8_t spec[jump::kSpecPkgSize];
	At<CopyFn>(sfv::kCopySpecPkg)(spec, current);
	const std::string before = Narrow(spec + jump::kSpecStage);
	const MatchRules& rules = picks.rules;
	if (!rules.stage.empty()) AssignString(spec + jump::kSpecStage, rules.stage);
	if (rules.rounds > 0) Field<int32_t>(spec, jump::kSpecRounds) = rules.rounds;
	if (rules.timeFrames != -1) Field<int32_t>(spec, jump::kSpecTime) = rules.timeFrames;
	Virtual<void>(modeData, jump::kDataSetSpec, (const uint8_t*)spec);
	Log("launch: stage %s -> %s (time %d, rounds %d)", before.c_str(), Narrow(spec + jump::kSpecStage).c_str(),
		Field<int32_t>(spec, jump::kSpecTime), Field<int32_t>(spec, jump::kSpecRounds));
	FreeGameBlock(spec, jump::kSpecStage);
}

// After the second step the flow's loader is the new mode's, with a fresh
// data object; the next step copies it into the battle setting.
// Game thread: from the picks to the battle. The jump state stays current
// while the game loads; leaving it for the main menu, or never leaving it,
// means no battle came, which a linked match must hear about rather than
// wait for. Leaving it for a battle state means the load is done (the
// resource load, rva 0x69BC20, put back any costume it would at its start),
// and the looks it loaded are logged (LogLoadedLooks).
void WatchLoading(PlayerLooks written, LaunchFailed onFailed, bool gameCheck)
{
	auto frames = std::make_shared<int>(0);
	RunEachGameFrame([written = std::move(written), onFailed = std::move(onFailed), frames, gameCheck]() mutable {
		uint8_t* state = CurrentSceneState();
		const uintptr_t kind = SceneStateKind(state);
		// The check runs in the step after the second (sfv::jump::
		// kJumpCheckResult); its result, once, the first frame after it.
		if (gameCheck && *frames == 1 && kind == sfv::scene::kJumpStateVtable) {
			Log("launch: the game's ownership check gave 0x%x", Field<uint32_t>(state, jump::kJumpCheckResult));
		}
		if (kind == sfv::scene::kVersusBattleVtable || kind == sfv::scene::kTrainingBattleVtable) {
			LogLoadedLooks(written);
			return false;
		}
		// Another state after the jump (not a battle's, whose looks are read
		// back above): the watch ends, and says so, so that two copies' logs
		// show a missing read-back rather than nothing.
		if (kind != sfv::scene::kMainMenuVtable && kind != sfv::scene::kJumpStateVtable) {
			Log("launch: left the jump for state 0x%llX; looks not read back", (unsigned long long)kind);
			return false;
		}
		const char* why = kind == sfv::scene::kMainMenuVtable ? "the game went back to the main menu while loading"
			: ++*frames >= kLoadFrames                        ? "the battle did not load within a minute"
															  : nullptr;
		if (!why) return true; // still loading
		Log("launch: failed: %s", why);
		s_linkedLoading = s_replayLoading = false; // no battle came for it
		if (onFailed) onFailed(why);
		return false;
	});
}

// Both copies of a room's battle, and a replay's playback, start from the
// same carried count (gameinstance::kCarriedCount): 0, what a game just
// started has, so the opening level-ups depend on nothing a PC's menus left.
void ClearCarriedCount()
{
	uint8_t* instance = At<GameInstanceFn>(sfv::kGameInstance)();
	if (!instance) return Log("launch: no game instance; the carried count stays as the menus left it");
	int32_t& count = Field<int32_t>(instance, gameinstance::kCarriedCount);
	if (count != 0) Log("launch: the carried count was %d; a room's battle and a replay start from 0", count);
	count = 0;
}

void* h_JumpSecondStep(uint8_t* jumpState)
{
	void* next = o_jumpSecondStep(jumpState);
	if (!IsOurJump(jumpState)) return next;
	GameRef flow{};
	At<GetFlowFn>(sfv::kGetBattleFlow)(&flow);
	uint8_t* modeData = flow.object ? ModeData(flow.object) : nullptr;
	if (const std::string refusal = ModeDataRefusal(modeData, s_pending.picks.target); !refusal.empty()) {
		// Too late to go back: the battle loads with the mode's defaults.
		GiveBackGameRef(flow);
		FailLaunch(refusal);
		return next;
	}
	const MatchPicks picks = std::move(s_pending.picks);
	LaunchFailed onFailed = std::move(s_pending.onFailed);
	s_pending = Pending{};
	PlayerLooks written;
	for (int player = 0; player < jump::kPlayers; player++) written[player] = WriteFighter(modeData, player, picks.players[player]);
	WriteStage(modeData, picks);
	GiveBackGameRef(flow);
	if (picks.linked || picks.replay) ClearCarriedCount();
	s_linkedLoading = picks.linked;
	s_replayLoading = picks.replay;
	s_linkedPractice = picks.linked && picks.practice;
	uint8_t* message = Field<uint8_t*>(jumpState, jump::kJumpStateMessage);
	WatchLoading(std::move(written), std::move(onFailed), message && Field<uint8_t>(message, jump::kMessageCheckOwnership) != 0);
	return next;
}

bool StagePlayable(uint8_t* singleton, const std::wstring& code)
{
	for (const wchar_t* unplayable : sfv::content::kUnplayableStages) {
		if (_wcsicmp(code.c_str(), unplayable) == 0) return false;
	}
	uint8_t* stages = Field<uint8_t*>(singleton, sfv::content::kStageAsset);
	if (!stages) return false;
	uint8_t* entries = Field<uint8_t*>(stages, sfv::content::kStageEntries);
	const int count = Field<int32_t>(stages, sfv::content::kStageCount);
	for (int i = 0; entries && i < count; i++) {
		const GameString& entry = Field<GameString>(entries + sfv::content::kStageStride * i, 0);
		if (entry.data && entry.count > 1 && _wcsicmp(entry.data, code.c_str()) == 0) return true;
	}
	return false;
}

// Why a battle on this copy would not load the look (LookLoadState), for
// the refusal after "player N's"; empty for one it loads as picked.
std::string LookRefusal(LookLoad state)
{
	switch (state) {
	case LookLoad::Loadable: return {};
	case LookLoad::NoTables: return "fighter cannot be checked: the game's content tables or spec data are not loaded";
	case LookLoad::NotBattleFighter: return "fighter is not one a battle can load";
	case LookLoad::FighterNotInstalled: return "fighter is not installed";
	case LookLoad::CostumeNotInSpec: return "costume is not in the game's spec";
	case LookLoad::CostumeNotInstalled: return "costume's package is not installed here";
	case LookLoad::ColourNotInSpec: return "colour is not in the game's spec for that costume";
	}
	return "look is not one this copy knows";
}

// Empty when the game has every pick (no ownership involved: the game's
// content tables and installed packages), else why not. A launch with
// content the game does not have would load nothing sensible. Each look is
// GameContent's LookLoadState, one lookup of the fighter: a fighter a battle
// can load (BattleFighter, the rule the catalog is built by, so the pipe's
// launch cannot reach a fighter a room would never offer) and installed
// here, a costume with a spec row and its package here (the battle's own
// load would otherwise put it back to 0 without a word), a colour with a
// spec row. None of it asks the account's entitlement cache, so an
// opponent's costume this copy's account lacks still loads as picked
// (EMBER_LIBRARY.md section 41). How many V-Skills and V-Triggers the fighter
// has is FighterChoiceCounts'.
std::string MissingContent(const MatchPicks& picks)
{
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	if (!singleton) return "the game's content tables are not loaded";
	for (int player = 0; player < jump::kPlayers; player++) {
		const FighterPick& pick = picks.players[player];
		if (pick.fighter.empty()) continue;
		const std::string who = "player " + std::to_string(player + 1) + "'s ";
		if (const std::string refusal = LookRefusal(LookLoadState(pick.fighter, pick.costume, pick.colour)); !refusal.empty()) {
			return who + refusal;
		}
		const std::optional<ChoiceCounts> counts = FighterChoiceCounts(pick.fighter);
		if (!counts) return who + "fighter's V-Skills and V-Triggers are not known (the game's name assets are not there, or did not list it)";
		if (pick.vSkill < 0 || pick.vSkill >= counts->vSkills || pick.vTrigger < 0 || pick.vTrigger >= counts->vTriggers) {
			return who + "fighter has " + std::to_string(counts->vSkills) + " V-Skills and " + std::to_string(counts->vTriggers) +
				" V-Triggers, so no V-Skill " + std::to_string(pick.vSkill + 1) + " with V-Trigger " + std::to_string(pick.vTrigger + 1);
		}
	}
	if (!picks.rules.stage.empty() && !StagePlayable(singleton, picks.rules.stage)) return "the stage is not a playable stage";
	return std::string();
}

// Game thread: from the main menu into the battle jump. Until the jump's
// second step takes the picks, a watcher ends a launch the game did not
// carry out (another scene came instead, or it never got that far).
void Launch(const MatchPicks& picks, LaunchFailed onFailed)
{
	auto refuse = [&](const char* why) {
		Log("launch: refused, %s", why);
		if (onFailed) onFailed(why);
	};
	if (!s_installed) return refuse("the battle jump is not hooked");
	uint8_t* menu = CurrentSceneState();
	if (SceneStateKind(menu) != sfv::scene::kMainMenuVtable) return refuse("the game is not on the main menu");
	if (s_pending.message) return refuse("a launch is already under way");
	if (SceneChangePending()) return refuse("the main menu is already changing scene");
	const std::string missing = MissingContent(picks);
	if (!missing.empty()) return refuse(missing.c_str());
	uint8_t* messageClass = At<GetClassFn>(sfv::kJumpMessageClass)(L"/Script/KiwiGame");
	uint8_t* jumpClass = At<GetClassFn>(sfv::kJumpStateClass)(L"/Script/KiwiGame");
	uint8_t* message = messageClass ? At<NewObjectFn>(sfv::kNewObject)(menu, messageClass) : nullptr;
	if (!jumpClass || !message) return refuse("the battle jump's classes are missing");
	Field<int32_t>(message, jump::kMessageMode) = (int32_t)picks.target;
	Field<uint8_t>(message, jump::kMessageCopySetting) = 0;
	Field<uint8_t>(message, jump::kMessageSwapSides) = 0;
	// A room's match never runs the game's check: it judges both players by
	// this copy's content and may rewrite either pick (and the stage, by
	// rand()), so two copies would load different battles. Each player's own
	// pick was judged by their own copy before Ready (EMBER_LIBRARY.md
	// section 23); the picks both copies agreed are the picks loaded.
	// A replay plays the battle that was recorded, so the check does not run
	// for it either.
	const bool gameCheck = s_checkOwnership && !picks.linked && !picks.replay;
	Field<uint8_t>(message, jump::kMessageCheckOwnership) = gameCheck ? 1 : 0;
	if (!ChangeScene(jumpClass, message)) return refuse("the scene manager is not known");
	s_pending = Pending{message, false, picks, std::move(onFailed)};
	Log("launch: battle jump, target %d, %s", (int)picks.target,
		picks.linked ? "a room's match: the game's ownership check does not run"
		: picks.replay ? "a replay: the game's ownership check does not run"
		: gameCheck  ? "the game's ownership check runs"
					 : "the game's ownership check does not run");
	auto frames = std::make_shared<int>(0);
	RunEachGameFrame([message, frames] {
		if (s_pending.message != message) return false; // done, or failed already
		const uintptr_t kind = SceneStateKind(CurrentSceneState());
		const bool onTheWay = kind == sfv::scene::kMainMenuVtable || kind == sfv::scene::kJumpStateVtable;
		if (!onTheWay) FailLaunch("the game went to another scene");
		else if (++*frames >= kLaunchFrames) FailLaunch("the jump did not reach its second step");
		else return true;
		return false;
	});
}

constexpr size_t kMaxCode = 8;

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

// A costume or colour number, 0 to 255 (a room carries it in a byte), or
// "-"/nothing for the default.
bool ParseVariant(const std::string& text, int* value)
{
	if (text.empty() || text == "-") return *value = -1, true;
	if (text.size() > 3 || text.find_first_not_of("0123456789") != std::string::npos) return false;
	*value = atoi(text.c_str());
	return *value <= 0xFF;
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

// A V-Skill or V-Trigger as the game calls it, "1" for I or "2" for II, into
// its number; "-"/nothing for I.
bool ParsePart(const std::string& text, int* number)
{
	if (text.empty() || text == "-") return *number = 0, true;
	if (text != "1" && text != "2") return false;
	*number = text[0] - '1';
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

} // namespace

void FillCharPkg(uint8_t* chara, const FighterPick& pick)
{
	if (pick.costume >= 0) Field<int32_t>(chara, jump::kCharCostume) = pick.costume;
	if (pick.colour >= 0) Field<int32_t>(chara, jump::kCharColour) = pick.colour;
	Field<int32_t>(chara, jump::kCharVSkill) = pick.vSkill;
	Field<int32_t>(chara, jump::kCharVTrigger) = pick.vTrigger;
}

bool LookMismatch(const PlayerLook& written, const PlayerLook& loaded)
{
	return !SameCode(written.fighter, loaded.fighter) || written.costume != loaded.costume || written.colour != loaded.colour;
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

bool InstallVersusLaunch(uintptr_t imageBase, bool gameCheckOutsideRooms)
{
	s_base = imageBase;
	s_checkOwnership = gameCheckOutsideRooms;
	Log("launch: the game's ownership check %s; a room's match never runs it",
		gameCheckOutsideRooms ? "runs in launches that are not a room's" : "does not run");
	// Both steps of the battle jump or neither (GameHooks' one set path).
	HookTransaction hooks;
	hooks.Add(imageBase, sfv::kJumpCopyPlayers, (void*)h_JumpCopyPlayers, (void**)&o_jumpCopyPlayers);
	hooks.Add(imageBase, sfv::kJumpSecondStep, (void*)h_JumpSecondStep, (void**)&o_jumpSecondStep);
	s_installed = !CommitHookSet(hooks, "battle jump");
	return s_installed;
}

void QueueLaunch(const MatchPicks& picks, LaunchFailed onFailed)
{
	RunOnGameThread([picks, onFailed = std::move(onFailed)]() mutable { Launch(picks, std::move(onFailed)); });
}

void LaunchOnMainMenu(const MatchPicks& picks, std::function<bool()> stillWanted, LaunchFailed onFailed)
{
	auto frames = std::make_shared<int>(0);
	RunEachGameFrame([picks, stillWanted = std::move(stillWanted), onFailed = std::move(onFailed), frames]() mutable {
		if (stillWanted && !stillWanted()) {
			Log("launch: no longer wanted before the main menu came");
			return false;
		}
		if (SceneStateKind(CurrentSceneState()) == sfv::scene::kMainMenuVtable) {
			Launch(picks, std::move(onFailed));
			return false;
		}
		if (++*frames < kWaitForMenuFrames) return true;
		Log("launch: refused, the main menu did not come");
		if (onFailed) onFailed("the main menu did not come");
		return false;
	});
}

bool TakeLinkedBattle(bool* practice)
{
	const bool linked = s_linkedLoading;
	if (practice) *practice = linked && s_linkedPractice;
	s_linkedLoading = s_linkedPractice = false;
	return linked;
}

bool TakeReplayBattle()
{
	const bool replay = s_replayLoading;
	s_replayLoading = false;
	return replay;
}

void RegisterVersusLaunchCommands()
{
	RegisterCommand("launch", [](const std::string& args) {
		std::istringstream in(args);
		std::string target, p1, p2, stage, rounds, time;
		in >> target >> p1 >> p2 >> stage >> rounds >> time;
		MatchPicks picks;
		if (target == "cpu") picks.target = LaunchTarget::VersusCpu;
		else if (target == "training") picks.target = LaunchTarget::Training;
		else if (!target.empty() && target != "versus") {
			return std::string("error usage: launch [versus|cpu|training] [P1] [P2] [stage] [rounds] [seconds|inf]");
		}
		if (!ParsePick(p1, &picks.players[0]) || !ParsePick(p2, &picks.players[1]) ||
			(!stage.empty() && !ParseCode(stage, &picks.rules.stage)) || !ParseRounds(rounds, &picks.rules.rounds) ||
			!ParseTime(time, &picks.rules.timeFrames)) {
			return std::string("error a player is CODE[:costume[:colour[:vskill[:vtrigger]]]] (vskill and vtrigger 1|2) or -, "
							   "a stage CODE or -, rounds 1|3|5|7, time 30-546 s or inf");
		}
		QueueLaunch(picks);
		return std::string("ok queued");
	});
}

} // namespace sfve::mod
