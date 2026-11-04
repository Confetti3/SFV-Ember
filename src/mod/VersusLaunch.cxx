#include "mod/VersusLaunch.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <stdlib.h>

#include <sstream>

namespace sfve::mod {

namespace {

namespace jump = sfv::jump;

// The game's FString: {characters, count including the terminator, capacity}.
struct GameString {
	wchar_t* data;
	int32_t count;
	int32_t capacity;
};

using GetClassFn = uint8_t* (*)(const wchar_t* package);
using NewObjectFn = uint8_t* (*)(uint8_t* outer, uint8_t* objectClass);
using JumpStepFn = void* (*)(uint8_t* jumpState);
using GetFlowFn = GameSharedPtr* (*)(GameSharedPtr* out);
using EnsureLoaderFn = void (*)(uint8_t* flow, int providerType);
using GameDataFn = uint8_t* (*)();
using PlayerRefFn = uint8_t* (*)(uint8_t* gameData, int player);
using GetPlayerSetupFn = void (*)(uint8_t* playerRef, uint8_t* setup);
using CopyFn = uint8_t* (*)(uint8_t* out, const uint8_t* from);
using AssignStringFn = void (*)(uint8_t* to, const GameString* from, int extra);
using FreeFn = void (*)(void* block);

uintptr_t s_base;
JumpStepFn o_jumpCopyPlayers;
JumpStepFn o_jumpSecondStep;

// Game thread only: the launch in progress, from Launch until the second
// step has written its picks.
struct Pending {
	uint8_t* message = nullptr; // the init message the mod made (the jump keeps it)
	bool loaderGiven = false;
	MatchPicks picks;
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
	const GameString from{const_cast<wchar_t*>(text.c_str()), (int32_t)text.size() + 1, (int32_t)text.size() + 1};
	At<AssignStringFn>(sfv::kAssignString)(gameString, &from, 0);
}

std::string Narrow(const uint8_t* gameString)
{
	const GameString& s = *reinterpret_cast<const GameString*>(gameString);
	std::string out;
	for (int i = 0; s.data && i < s.count && s.data[i]; i++) out += (char)s.data[i];
	return out;
}

// The game's own reference blocks for the battle flow: {int32 uses, int32
// weaks, object, destroy}. Gives back the use the getter took (never the
// last: the flow is the game's). The game counts these without atomics, on
// this same thread.
void GiveBackFlowUse(uint8_t* control)
{
	if (control && *reinterpret_cast<int32_t*>(control) > 1) (*reinterpret_cast<int32_t*>(control))--;
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

// The first step copies the players from the flow's loader into the battle
// setting. Without a loader (the main menu reset the flow) it would read
// through null; give it one holding the setting's own players, so the copy
// changes nothing.
void* h_JumpCopyPlayers(uint8_t* jumpState)
{
	if (IsOurJump(jumpState) && !s_pending.loaderGiven) {
		s_pending.loaderGiven = true;
		GameSharedPtr flow{};
		At<GetFlowFn>(sfv::kGetBattleFlow)(&flow);
		if (flow.object && !Field<uint8_t*>(flow.object, jump::kFlowLoader)) {
			At<EnsureLoaderFn>(sfv::kEnsureLoader)(flow.object, jump::kProviderType);
			uint8_t* provider = ModeData(flow.object);
			uint8_t* gameData = At<GameDataFn>(sfv::kGameData)();
			for (int player = 0; provider && gameData && player < jump::kPlayers; player++) {
				uint8_t* ref = At<PlayerRefFn>(sfv::kPlayerRef)(gameData, player);
				At<GetPlayerSetupFn>(sfv::kGetPlayerSetup)(ref, provider + jump::kProviderPlayers + jump::kPlayerSetupSize * player);
			}
			if (!provider) Log("launch: the flow has no provider of type %d; the jump will fail", jump::kProviderType);
		}
		GiveBackFlowUse(flow.control);
	}
	return o_jumpCopyPlayers(jumpState);
}

void WriteFighter(uint8_t* modeData, int player, const FighterPick& pick)
{
	alignas(8) uint8_t chara[jump::kCharPkgSize];
	At<CopyFn>(sfv::kCopyCharPkg)(chara, Virtual<const uint8_t*>(modeData, jump::kDataGetChara, player));
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
	FreeGameBlock(chara, jump::kCharCode);
	FreeGameBlock(chara, jump::kCharItems);
}

void WriteStage(uint8_t* modeData, const std::wstring& stage)
{
	alignas(8) uint8_t spec[jump::kSpecPkgSize];
	At<CopyFn>(sfv::kCopySpecPkg)(spec, Virtual<const uint8_t*>(modeData, jump::kDataGetSpec));
	const std::string before = Narrow(spec + jump::kSpecStage);
	if (!stage.empty()) AssignString(spec + jump::kSpecStage, stage);
	Virtual<void>(modeData, jump::kDataSetSpec, (const uint8_t*)spec);
	Log("launch: stage %s -> %s", before.c_str(), Narrow(spec + jump::kSpecStage).c_str());
	FreeGameBlock(spec, jump::kSpecStage);
}

// After the second step the flow's loader is the new mode's, with a fresh
// data object; the next step copies it into the battle setting.
void* h_JumpSecondStep(uint8_t* jumpState)
{
	void* next = o_jumpSecondStep(jumpState);
	if (!IsOurJump(jumpState)) return next;
	const MatchPicks picks = s_pending.picks;
	s_pending = Pending{};
	GameSharedPtr flow{};
	At<GetFlowFn>(sfv::kGetBattleFlow)(&flow);
	uint8_t* modeData = flow.object ? ModeData(flow.object) : nullptr;
	if (!modeData) {
		Log("launch: the jump has no mode data; the battle uses the defaults");
	} else {
		Log("launch: mode data type %d", Virtual<int>(modeData, jump::kDataType));
		for (int player = 0; player < jump::kPlayers; player++) WriteFighter(modeData, player, picks.players[player]);
		WriteStage(modeData, picks.stage);
	}
	GiveBackFlowUse(flow.control);
	return next;
}

// Game thread: from the main menu into the battle jump.
void Launch(const MatchPicks& picks)
{
	uint8_t* menu = CurrentSceneState();
	if (SceneStateKind(menu) != sfv::scene::kMainMenuVtable) return Log("launch: refused, the game is not on the main menu");
	if (s_pending.message) return Log("launch: refused, a launch is already under way");
	uint8_t* messageClass = At<GetClassFn>(sfv::kJumpMessageClass)(L"/Script/KiwiGame");
	uint8_t* jumpClass = At<GetClassFn>(sfv::kJumpStateClass)(L"/Script/KiwiGame");
	uint8_t* message = messageClass ? At<NewObjectFn>(sfv::kNewObject)(menu, messageClass) : nullptr;
	if (!jumpClass || !message) return Log("launch: the battle jump's classes are missing");
	Field<int32_t>(message, jump::kMessageMode) = (int32_t)picks.target;
	Field<uint8_t>(message, jump::kMessageCopySetting) = 0;
	Field<uint8_t>(message, jump::kMessageSwapSides) = 0;
	Field<uint8_t>(message, jump::kMessageCheckOwnership) = 1;
	s_pending = Pending{message, false, picks};
	Log("launch: battle jump, target %d", (int)picks.target);
	ChangeScene(jumpClass, message);
}

// "CODE[:costume[:colour]]" or "-".
FighterPick ParsePick(const std::string& word)
{
	FighterPick pick;
	if (word.empty() || word == "-") return pick;
	std::istringstream in(word);
	std::string code, costume, colour;
	std::getline(in, code, ':');
	std::getline(in, costume, ':');
	std::getline(in, colour, ':');
	pick.fighter.assign(code.begin(), code.end());
	if (!costume.empty()) pick.costume = atoi(costume.c_str());
	if (!colour.empty()) pick.colour = atoi(colour.c_str());
	return pick;
}

} // namespace

bool InstallVersusLaunch(uintptr_t imageBase)
{
	s_base = imageBase;
	return HookCodeSite(imageBase, sfv::kJumpCopyPlayers, (void*)h_JumpCopyPlayers, (void**)&o_jumpCopyPlayers) &&
		HookCodeSite(imageBase, sfv::kJumpSecondStep, (void*)h_JumpSecondStep, (void**)&o_jumpSecondStep);
}

void QueueLaunch(const MatchPicks& picks)
{
	RunOnGameThread([picks] { Launch(picks); });
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
		picks.players[0] = ParsePick(p1);
		picks.players[1] = ParsePick(p2);
		if (stage != "-") picks.stage.assign(stage.begin(), stage.end());
		QueueLaunch(picks);
		return std::string("ok queued");
	});
}

} // namespace sfve::mod
