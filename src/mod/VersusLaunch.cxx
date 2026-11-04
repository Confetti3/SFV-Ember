#include "mod/VersusLaunch.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <stdlib.h>
#include <wchar.h>

#include <memory>
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
using FighterInstalledFn = bool (*)(uint8_t* charaAsset, const GameString* code);
using CostumeValidFn = bool (*)(void* unused, const GameString* code, int costume);
using ColourValidFn = bool (*)(void* unused, const GameString* code, int costume, int colour);

// A launch that has not reached the jump's second step by then has failed
// (the jump is normally there within a few frames).
constexpr int kLaunchFrames = 60 * 10;
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

void WriteFighter(uint8_t* modeData, int player, const FighterPick& pick)
{
	const uint8_t* current = Virtual<const uint8_t*>(modeData, jump::kDataGetChara, player);
	if (!current) return Log("launch: player %d has no fighter record; left as it is", player + 1);
	alignas(8) uint8_t chara[jump::kCharPkgSize];
	At<CopyFn>(sfv::kCopyCharPkg)(chara, current);
	const std::string before = Narrow(chara + jump::kCharCode);
	if (!pick.fighter.empty()) AssignString(chara + jump::kCharCode, pick.fighter);
	if (pick.costume >= 0) Field<int32_t>(chara, jump::kCharCostume) = pick.costume;
	if (pick.colour >= 0) Field<int32_t>(chara, jump::kCharColour) = pick.colour;
	// The game's own order (rva 0x602C80, 0x6C8540): the fighter's setup,
	// then the selected fighter. Both copy the record.
	Virtual<void>(modeData, jump::kDataSetChara, player, (const uint8_t*)chara);
	Virtual<void>(modeData, jump::kDataSetCode, player, (const uint8_t*)chara);
	Log("launch: player %d %s -> %s costume %d colour %d", player + 1, before.c_str(), Narrow(chara + jump::kCharCode).c_str(),
		Field<int32_t>(chara, jump::kCharCostume), Field<int32_t>(chara, jump::kCharColour));
	// What the copy allocated: the code's characters and the items' array
	// (its 16-byte items hold no pointers: the copy is a memcpy).
	FreeGameBlock(chara, jump::kCharCode);
	FreeGameBlock(chara, jump::kCharItems);
}

void WriteStage(uint8_t* modeData, const MatchPicks& picks)
{
	const uint8_t* current = Virtual<const uint8_t*>(modeData, jump::kDataGetSpec);
	if (!current) return Log("launch: no match record; the stage is left as it is");
	alignas(8) uint8_t spec[jump::kSpecPkgSize];
	At<CopyFn>(sfv::kCopySpecPkg)(spec, current);
	const std::string before = Narrow(spec + jump::kSpecStage);
	if (!picks.stage.empty()) AssignString(spec + jump::kSpecStage, picks.stage);
	Virtual<void>(modeData, jump::kDataSetSpec, (const uint8_t*)spec);
	Log("launch: stage %s -> %s (time %d, rounds %d)", before.c_str(), Narrow(spec + jump::kSpecStage).c_str(),
		Field<int32_t>(spec, jump::kSpecTime), Field<int32_t>(spec, jump::kSpecRounds));
	FreeGameBlock(spec, jump::kSpecStage);
}

// After the second step the flow's loader is the new mode's, with a fresh
// data object; the next step copies it into the battle setting.
void* h_JumpSecondStep(uint8_t* jumpState)
{
	void* next = o_jumpSecondStep(jumpState);
	if (!IsOurJump(jumpState)) return next;
	GameRef flow{};
	At<GetFlowFn>(sfv::kGetBattleFlow)(&flow);
	uint8_t* modeData = flow.object ? ModeData(flow.object) : nullptr;
	const int wantedType = kDataTypeForTarget[(int)s_pending.picks.target];
	const int type = modeData ? Virtual<int>(modeData, jump::kDataType) : -1;
	if (type != wantedType) {
		// Too late to go back: the battle loads with the mode's defaults.
		GiveBackGameRef(flow);
		FailLaunch("the jump's mode data is type " + std::to_string(type) + ", not " + std::to_string(wantedType));
		return next;
	}
	const MatchPicks picks = std::move(s_pending.picks);
	s_pending = Pending{};
	for (int player = 0; player < jump::kPlayers; player++) WriteFighter(modeData, player, picks.players[player]);
	WriteStage(modeData, picks);
	GiveBackGameRef(flow);
	return next;
}

GameString View(const std::wstring& text)
{
	return GameString{text.c_str(), (int32_t)text.size() + 1, (int32_t)text.size() + 1};
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

// Empty when the game has every pick (no ownership involved: the game's
// content tables and installed packages), else why not. A launch with
// content the game does not have would load nothing sensible.
std::string MissingContent(const MatchPicks& picks)
{
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	uint8_t* fighters = singleton ? Field<uint8_t*>(singleton, sfv::content::kCharaAsset) : nullptr;
	if (!fighters) return "the game's content tables are not loaded";
	for (int player = 0; player < jump::kPlayers; player++) {
		const FighterPick& pick = picks.players[player];
		if (pick.fighter.empty()) continue;
		const GameString code = View(pick.fighter);
		const std::string who = "player " + std::to_string(player + 1) + "'s ";
		if (_wcsicmp(pick.fighter.c_str(), sfv::content::kRandomFighter) == 0 ||
			!At<FighterInstalledFn>(sfv::kFighterInstalled)(fighters, &code)) {
			return who + "fighter is not in the game";
		}
		const int costume = pick.costume < 0 ? 0 : pick.costume;
		if (pick.costume >= 0 && !At<CostumeValidFn>(sfv::kCostumeValid)(nullptr, &code, costume)) {
			return who + "costume is not in the game";
		}
		if (pick.colour >= 0 && !At<ColourValidFn>(sfv::kColourValid)(nullptr, &code, costume, pick.colour)) {
			return who + "colour is not in the game";
		}
	}
	if (!picks.stage.empty() && !StagePlayable(singleton, picks.stage)) return "the stage is not a playable stage";
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
	Field<uint8_t>(message, jump::kMessageCheckOwnership) = s_checkOwnership ? 1 : 0;
	if (!ChangeScene(jumpClass, message)) return refuse("the scene manager is not known");
	s_pending = Pending{message, false, picks, std::move(onFailed)};
	Log("launch: battle jump, target %d", (int)picks.target);
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

// A costume or colour: up to two digits, or "-"/nothing for the default.
bool ParseVariant(const std::string& text, int* value)
{
	if (text.empty() || text == "-") return *value = -1, true;
	if (text.size() > 2 || text.find_first_not_of("0123456789") != std::string::npos) return false;
	*value = atoi(text.c_str());
	return true;
}

// "CODE[:costume[:colour]]" or "-".
bool ParsePick(const std::string& word, FighterPick* pick)
{
	*pick = FighterPick{};
	if (word.empty() || word == "-") return true;
	std::istringstream in(word);
	std::string code, costume, colour, extra;
	std::getline(in, code, ':');
	std::getline(in, costume, ':');
	std::getline(in, colour, ':');
	return !std::getline(in, extra, ':') && ParseCode(code, &pick->fighter) && ParseVariant(costume, &pick->costume) &&
		ParseVariant(colour, &pick->colour);
}

} // namespace

bool ParseSetup(const std::string& setup, FighterPick* fighter, std::wstring* stage)
{
	std::istringstream in(setup);
	std::string pick, stageCode, extra;
	in >> pick >> stageCode;
	stage->clear();
	return !(in >> extra) && ParsePick(pick, fighter) && (stageCode.empty() || ParseCode(stageCode, stage));
}

bool InstallVersusLaunch(uintptr_t imageBase, bool checkOwnership)
{
	s_base = imageBase;
	s_checkOwnership = checkOwnership;
	Log("launch: the game's ownership check %s", checkOwnership ? "runs" : "is skipped (development)");
	s_installed = HookCodeSite(imageBase, sfv::kJumpCopyPlayers, (void*)h_JumpCopyPlayers, (void**)&o_jumpCopyPlayers) &&
		HookCodeSite(imageBase, sfv::kJumpSecondStep, (void*)h_JumpSecondStep, (void**)&o_jumpSecondStep);
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

void RegisterVersusLaunchCommands()
{
	RegisterCommand("launch", [](const std::string& args) {
		std::istringstream in(args);
		std::string target, p1, p2, stage;
		in >> target >> p1 >> p2 >> stage;
		MatchPicks picks;
		if (target == "cpu") picks.target = LaunchTarget::VersusCpu;
		else if (target == "training") picks.target = LaunchTarget::Training;
		else if (!target.empty() && target != "versus") return std::string("error usage: launch [versus|cpu|training] [P1] [P2] [stage]");
		if (!ParsePick(p1, &picks.players[0]) || !ParsePick(p2, &picks.players[1]) ||
			(!stage.empty() && !ParseCode(stage, &picks.stage))) {
			return std::string("error a player is CODE[:costume[:colour]] or -, a stage CODE or -");
		}
		QueueLaunch(picks);
		return std::string("ok queued");
	});
}

} // namespace sfve::mod
