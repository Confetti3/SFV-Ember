#include "ember/SfvTraining.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvPick.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/SfvOffsets.hxx"

#include "common/Localization.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "sf4e/sf4e__RuntimeEvents.hxx"
#include "training/TrainingSession.hxx"

#include <atomic>
#include <mutex>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;

// SF4 Ember's count (TrainingRuntime.cxx: LeaveFrames): two seconds of the
// banner before the battle leaves, at the 60 frames a second both games run.
constexpr int kLeaveFrames = 120;
// A Training battle whose last update is older than this many game frames is
// not running (loading, or gone): one update a frame is the rule, and the
// scene manager's tick pumps the game-thread work one frame after the
// update it follows.
constexpr std::uint64_t kStaleFrames = 3;

std::mutex s_mutex;
sf4e::training::CallControl s_call; // the room's call owner's, as last set (SetCall)
TrainingView s_view;                 // as Follow last published it
bool s_paused = false;
std::atomic<bool> s_selectRequested{false};

// Game thread only: the Training battle followed, and its countdown.
std::uint64_t s_battle = 0;
bool s_leaving = false; // told to leave for the main menu
sf4e::training::LeaveCountdown s_countdown;

std::wstring Wide(const std::string& code)
{
	return std::wstring(code.begin(), code.end());
}

void Publish(const TrainingView& view, bool paused)
{
	std::lock_guard lock(s_mutex);
	s_view = view;
	s_paused = paused;
}

// Once a frame, before the scene manager ticks the battle and after Ember's
// outer tick has set the call (SetCall): which Training battle runs, and the
// call's countdown on it.
bool Follow()
{
	const mod::BattleSeen seen = mod::LastBattleSeen();
	const bool scene = mod::SceneStateKind(mod::CurrentSceneState()) == mod::sfv::scene::kTrainingBattleVtable &&
		!mod::SceneChangePending();
	const bool running = scene && seen.battle != 0 && mod::GameFrameNumber() - seen.gameFrame <= kStaleFrames;
	if (!running) {
		if (s_battle) mod::Log("training: battle %llu is no longer running%s", (unsigned long long)s_battle,
			s_leaving ? " (it was told to leave)" : "");
		s_battle = 0;
		s_leaving = false;
		s_countdown.Reset();
		Publish({}, false);
		return true;
	}
	if (seen.battle != s_battle) {
		s_battle = seen.battle;
		s_leaving = false;
		s_countdown.Reset();
		mod::Log("training: battle %llu runs", (unsigned long long)s_battle);
	}
	sf4e::training::CallControl call;
	{
		std::lock_guard lock(s_mutex);
		call = s_call;
	}
	if (!s_leaving) {
		if (s_countdown.Follow(call, s_battle, kLeaveFrames)) {
			// SFV Ember has no sound module (SfvGame's PlayChallengerCall): the
			// banner alone tells the player.
			mod::Log("training: leaving for the main menu in %d frames for call %llu; no challenger call (no SFV sound)",
				kLeaveFrames, (unsigned long long)call.call);
		}
		// The pause menu's EXIT_BATTLE, asked for by Ember instead of the
		// player (SfvOffsets: kTrainingBattleVtable), and like SF4's only out
		// of a fight that is running: a battle still in its intro is waited for.
		if (s_countdown.Tick(seen.fighting)) {
			s_leaving = mod::LeaveBattle();
			mod::Log("training: battle %llu %s", (unsigned long long)s_battle,
				s_leaving ? "told to leave for the main menu" : "could not be told to leave (no scene manager)");
		}
	}
	TrainingView view;
	view.available = !s_leaving;
	view.generation = s_battle;
	view.leavingIn = s_countdown.Left();
	view.leavingCall = s_countdown.Call();
	Publish(view, seen.ticks == 0);
	return true;
}

class SfvTraining final : public ember::game::Training {
public:
	// From Home (no room): Ember's Training select, where the dummy is
	// chosen, as SF4's main menu leads to it. From a room: the battle at once,
	// with the pick the room has and the last dummy; the room stays joined.
	void Request(const sf4e::netplay::Generation&) override
	{
		const auto snapshot = facade::GetRuntimeSnapshotShared();
		const bool home = !snapshot || snapshot->session.room == sf4e::netplay::RoomState::Idle;
		if (home) {
			mod::Log("training: Home's Training: Ember's Training select");
			s_selectRequested = true;
			return;
		}
		const PlayerPick player = CurrentPick();
		LaunchTraining(FighterPickOf(player.pick), FighterPickOf(CurrentDummy().pick), Wide(OfflineStageCode(player.stage)),
			"a room's Wait in Training", "room.training.unavailable");
	}
	bool ControlsAvailable() override { return ReadTrainingView().available; }
	// A Practice room's game while its session is live (RoomBattle's launch
	// said so): the runtime's PracticeMatch, where Start opens Ember over the
	// fight with End practice.
	bool MatchPracticeActive() override { return PracticeBattleInPlay(); }
	sf4e::training::Place ReadPlace() override
	{
		sf4e::training::Place place;
		const TrainingView view = ReadTrainingView();
		if (view.available) place.generation = view.generation;
		return place;
	}
	void SetCall(const sf4e::training::CallControl& call) override
	{
		std::lock_guard lock(s_mutex);
		s_call = call;
	}
	// Nothing of SFV's Training is captured (it has no Ember lab).
	void StopCapture() override {}
};

SfvTraining s_training;

} // namespace

ember::game::Training& SfvTrainingService()
{
	return s_training;
}

void InstallSfvTraining()
{
	mod::RunEachGameFrame(Follow);
	// Status "training": the Training battle followed and the call's countdown.
	mod::RegisterStatus("training", [] {
		TrainingView view;
		bool paused = false;
		{
			std::lock_guard lock(s_mutex);
			view = s_view;
			paused = s_paused;
		}
		return "{\"available\":" + std::string(view.available ? "true" : "false") + ",\"battle\":" +
			std::to_string(view.generation) + ",\"leavingIn\":" + std::to_string(view.leavingIn) + ",\"call\":" +
			std::to_string(view.leavingCall) + ",\"paused\":" + (paused ? "true" : "false") + "}";
	});
}

TrainingView ReadTrainingView()
{
	std::lock_guard lock(s_mutex);
	return s_view;
}

bool TrainingPaused()
{
	std::lock_guard lock(s_mutex);
	return s_view.available && s_paused;
}

bool TakeTrainingSelectRequest()
{
	return s_selectRequested.exchange(false);
}

void LaunchTraining(const mod::FighterPick& player, const mod::FighterPick& dummy, const std::wstring& stage,
	const char* why, const char* notice)
{
	mod::MatchPicks picks;
	picks.target = mod::LaunchTarget::Training;
	picks.players[0] = player;
	picks.players[1] = dummy;
	picks.rules.stage = stage;
	// The V-Skill and V-Trigger as the pipe names them, 1 for I and 2 for II.
	mod::Log("training: %s: %ls (vskill %d vtrigger %d) vs %ls (vskill %d vtrigger %d) on %ls", why, player.fighter.c_str(),
		player.vSkill + 1, player.vTrigger + 1, dummy.fighter.c_str(), dummy.vSkill + 1, dummy.vTrigger + 1, stage.c_str());
	mod::QueueLaunch(picks, [notice](const std::string& failure) {
		mod::Log("training: the Training battle could not be loaded: %s", failure.c_str());
		facade::PushAlert(sf4e::loc::T(notice), sf4e::NoticeSeverity::Warning);
	});
}

} // namespace sfve::adapter
