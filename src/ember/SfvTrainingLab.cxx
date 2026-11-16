#include "ember/SfvTrainingLab.hxx"

#include "ember/SfvLabInput.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvTraining.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/SfvAdapter.hxx"
#include "mod/SfvDummyOffsets.hxx"

#include "training/LabCore.hxx"
#include "training/RecordingBank.hxx"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;
namespace dummy = mod::sfv::dummy;
using training::Action;
using training::Mode;

// The reply trace in the log, one line per frame a reply waits or the dummy
// is held (training::ReplyFrameTrace): with SFVE_TRAINING_TRACE=1 in the
// game's environment, read once, as SF4 Ember's SF4E_TRAINING_TRACE.
bool TraceOn()
{
	static const bool on = [] {
		char env[2] = {};
		return GetEnvironmentVariableA("SFVE_TRAINING_TRACE", env, sizeof env) == 1 && env[0] == '1';
	}();
	return on;
}

// What the lab does in one Training battle. Game thread only.
struct Lab {
	bool open = false;
	uint64_t battle = 0;
	training::Session session;
	training::DummyControl control;
	training::Acks acks;
	// This update: stepped (commands taken, input planned), the planned
	// input, the frame prepared from the pads in its first tick, and whether
	// it is to be committed after the update.
	bool stepped = false, prepared = false, commit = false;
	training::InputPlan plan;
	training::Frame output{};
	// Each slot as the last tick recorded it (the lab's words).
	std::array<unsigned, 2> recorded{};
	// Both fighters as the last simulated update left them (a playback's hit cue).
	std::array<training::FighterSample, 2> before{};
	// The dummy's control before the lab took it (DummyControl's Begin).
	lab::SideControl kept;
	// ExportSlot's frames, handed to the controls once per request.
	uint64_t exportId = 0;
	int exportedSlot = -1;
	std::vector<training::Input> exported;
	// What Ember plays, as the log names it.
	Mode loggedMode = Mode::Idle;
	int loggedSlot = -1;
	bool loggedReply = false;
	// Updates that simulated one tick, and gaps in the frames this battle
	// (updates that ran more than one tick).
	uint64_t frames = 0, gaps = 0;
};
// The last reply's timing (ReplyTrace) and the dummy's control as the lab
// last read them, for the pipe's status (another thread).
struct Told {
	std::atomic<int> sent{training::ReplyTiming::NotSeen}, freed{training::ReplyTiming::NotSeen}, move{training::ReplyTiming::NotSeen};
	std::atomic<int> timing{0}, replies{0}, kind{-9}, pad{-9}, action{-9};
};
Told s_told;
// A test's change of one of Training's own settings (RequestTrainingSetting),
// carried out by the next Step: index * 65536 + value, or -1 for none.
std::atomic<int> s_setting{-1};
// A test's request to leave the Training battle (RequestTrainingLeave), as
// the pause menu's Go to Main Menu does (mod::LeaveBattle).
std::atomic<bool> s_leave{false};
Lab s_lab;
// The dummy fighters' slots, kept between battles (RecordingBanks), and the
// reply's plan and draws, which outlive a battle. Game thread only.
training::RecordingBanks s_banks;
training::ReplyLab s_replies;
// Where the controls meet the game thread.
training::LabChannel s_channel;
std::atomic<bool> s_controlsOpen{false};

uint8_t* SideField(mod::SfvAdapter& adapter, int side, uint32_t field)
{
	uint8_t* data = adapter.TrainingBattleData();
	return data ? data + dummy::kSideStride * static_cast<uint32_t>(side) + field : nullptr;
}

std::optional<lab::SideControl> ReadControl(mod::SfvAdapter& adapter, int side)
{
	lab::SideControl control;
	uint8_t* kind = SideField(adapter, side, dummy::kSideKind);
	uint8_t* pad = SideField(adapter, side, dummy::kSidePad);
	if (!kind || !mod::TryRead(kind, &control.kind) || !mod::TryRead(pad, &control.pad)) return std::nullopt;
	return control;
}

bool WriteControl(mod::SfvAdapter& adapter, int side, const lab::SideControl& control)
{
	uint8_t* kind = SideField(adapter, side, dummy::kSideKind);
	uint8_t* pad = SideField(adapter, side, dummy::kSidePad);
	return kind && mod::TryWrite(kind, control.kind) && mod::TryWrite(pad, control.pad);
}

// Training's own dummy action (sfv::dummy::kActionSetting), -1 unread.
int ReadAction(mod::SfvAdapter& adapter)
{
	uint8_t* data = adapter.TrainingBattleData();
	int32_t action = -1;
	if (!data || !mod::TryRead(data + dummy::kTrainingSettings + dummy::kSettingStride * dummy::kActionSetting, &action)) return -1;
	return action;
}

// A Training setting other than the action (its 1..13), within the range the
// table gives it, as the pause menu's own setter writes it (rva 0x309390).
void ApplySetting(mod::SfvAdapter& adapter)
{
	const int request = s_setting.exchange(-1);
	if (request < 0) return;
	const int index = request >> 16, value = request & 0xFFFF;
	uint8_t* data = adapter.TrainingBattleData();
	std::array<int32_t, 4> entry{}; // value, default, min, max
	uint8_t* at = data ? data + dummy::kTrainingSettings + dummy::kSettingStride * static_cast<uint32_t>(index) : nullptr;
	const bool fits = at && index >= 1 && index <= 13 && mod::TryRead(at, &entry) && value >= entry[2] && value <= entry[3];
	if (fits && mod::TryWrite(at, static_cast<int32_t>(value)))
		mod::Log("training lab: Training setting %d set to %d (it was %d)", index, value, entry[0]);
	else mod::Log("training lab: Training setting %d = %d refused", index, value);
}

// The dummy's control back to what it was before the lab took it.
void GiveBack(mod::SfvAdapter& adapter)
{
	if (WriteControl(adapter, 1, s_lab.kept))
		mod::Log("training lab: the dummy's control back to kind %d, pad %d", s_lab.kept.kind, s_lab.kept.pad);
	else mod::Log("training lab: the dummy's control could not be given back");
}

void Publish(const training::MeterView* meter)
{
	const TrainingView battle = ReadTrainingView();
	s_channel.Publish([&](training::View& view) {
		view = s_lab.session.GetView();
		if (meter) view.meter = *meter;
		view.leavingIn = battle.leavingIn;
		view.leavingCall = battle.leavingCall;
		view.acks = s_lab.acks;
		view.exportId = s_lab.exportId;
		view.exportedSlot = s_lab.exportedSlot;
		view.exported = s_lab.exported;
	});
}

void Open(mod::SfvAdapter& adapter, uint64_t battle)
{
	s_lab = Lab{};
	s_lab.open = true;
	s_lab.battle = battle;
	// The dummy's slots from the last battle with this dummy (Training
	// select's; SFV's own character select may change it, and the slots stay
	// with the one Ember launched).
	const int fighter = CurrentDummy().pick.fighter;
	const bool kept = s_banks.Holds(fighter);
	s_lab.session.Enter(s_banks.Attach(fighter));
	s_replies.Close();
	s_replies.Seed(s_lab.session);
	const std::optional<lab::SideControl> control = ReadControl(adapter, 1);
	mod::Log("training lab: battle %llu, the dummy's slots %s; its control kind %d, pad %d, action %d",
		(unsigned long long)battle, kept ? "kept from its last battle" : "empty", control ? control->kind : -9,
		control ? control->pad : -9, ReadAction(adapter));
	Publish(nullptr);
}

// adapter: the battle's, null once it has gone.
void Close(mod::SfvAdapter* adapter)
{
	if (!s_lab.open) return;
	// The battle that ends may already be another one's match data: the
	// control goes back only while this battle is the adapter's.
	if (s_lab.control.Release() && adapter && adapter->Battle() == s_lab.battle) GiveBack(*adapter);
	s_banks.Detach(s_lab.session.TakeBank());
	s_replies.Close();
	mod::Log("training lab: battle %llu closed (%llu gaps in its frames)", (unsigned long long)s_lab.battle,
		(unsigned long long)s_lab.gaps);
	s_lab = Lab{};
	s_channel.Close(s_lab.session.GetView());
}

// Whether this is the lab's battle: SfvTraining's Training battle, which is
// the adapter's. The one test every write of the lab is behind.
bool LabBattle(mod::SfvAdapter& adapter)
{
	const TrainingView battle = ReadTrainingView();
	return battle.available && battle.generation != 0 && battle.generation == adapter.Battle() && adapter.Bound();
}

// One command from the controls, checked by the session first.
bool Dispatch(const training::Command& command)
{
	if (!s_lab.session.Apply(command)) return false;
	switch (command.action) {
	case Action::DummyPlan: s_replies.plan = command.plan; return true;
	case Action::ExportSlot:
		s_lab.exportedSlot = s_lab.session.GetView().selected;
		s_lab.exported = s_lab.session.Slot(s_lab.exportedSlot);
		++s_lab.exportId;
		return true;
	// SFV's lab has no position save, dummy settings or Combo check
	// (TrainingFeatures): a command for them is refused.
	// The way back to the room the player waits in (F10, Back to room): the
	// battle leaves for the main menu as a call's count ends it.
	case Action::LeaveToRoom: return LeaveTrainingToRoom();
	case Action::Place: case Action::Save: case Action::Restore: case Action::DummyState:
	case Action::ComboRecord: case Action::ComboStart: case Action::ComboStop: case Action::ComboPlay:
		return false;
	default: return true;
	}
}

void LogPlaying()
{
	const auto& view = s_lab.session.GetView();
	const int slot = training::PadPlaysDummy(view.mode) ? view.selected : view.active;
	if (view.mode == s_lab.loggedMode && slot == s_lab.loggedSlot && view.replying == s_lab.loggedReply) return;
	s_lab.loggedMode = view.mode;
	s_lab.loggedSlot = slot;
	s_lab.loggedReply = view.replying;
	const char* what = view.mode == Mode::Idle ? "idle" : view.mode == Mode::Standby ? "standby" : view.mode == Mode::Recording
		? "recording" : view.replying ? "reply" : "playback";
	mod::Log("training lab: %s, slot %d, Player %d", what, slot + 1, view.playbackSide + 1);
}

// Once an update, before its first tick reads the pads (or after an update
// that ran none): the battle followed, the controls' commands, the input
// planned and the dummy's control.
void Step(mod::SfvAdapter& adapter)
{
	s_lab.stepped = true;
	if (!LabBattle(adapter)) {
		Close(&adapter);
		return;
	}
	if (!s_lab.open || s_lab.battle != adapter.Battle()) {
		Close(&adapter);
		Open(adapter, adapter.Battle());
	}
	auto& session = s_lab.session;
	session.SetReady(adapter.Fighting());
	ApplySetting(adapter);
	const int action = ReadAction(adapter);
	s_told.action = action;
	if (const auto control = ReadControl(adapter, 1)) {
		s_told.kind = control->kind;
		s_told.pad = control->pad;
	}
	// A dummy action picked in SFV's pause menu while the lab played the
	// dummy stands: the game set the dummy's control from it, and the lab stops.
	if (s_lab.control.Reconcile(action)) {
		mod::Log("training lab: dummy action set to %d in the pause menu; the lab stops playing the dummy", action);
		session.Apply({Action::Stop, session.GetView().generation});
	}
	std::deque<training::Command> taken;
	std::vector<uint64_t> replaced;
	s_channel.Take(taken, replaced);
	training::RunBatch(training::PrepareBatch(taken, replaced, session.GetView().generation), s_lab.acks, Dispatch);
	s_lab.plan = training::PlanInput(session.GetView(), s_controlsOpen.load());
	LogPlaying();
	switch (s_lab.control.Follow(s_lab.plan.suppliesDummy, action)) {
	case training::DummyControl::Step::Begin:
		if (const auto control = ReadControl(adapter, 1)) s_lab.kept = *control;
		if (WriteControl(adapter, 1, lab::kLabControl))
			mod::Log("training lab: the dummy plays from its slot (control kind %d, pad %d before; action %d)", s_lab.kept.kind,
				s_lab.kept.pad, action);
		break;
	// The pause menu closing may set the control again from the action.
	case training::DummyControl::Step::Keep:
		if (const auto control = ReadControl(adapter, 1); control && *control != lab::kLabControl) {
			mod::Log("training lab: the dummy's control was set to kind %d, pad %d; set again", control->kind, control->pad);
			WriteControl(adapter, 1, lab::kLabControl);
		}
		break;
	case training::DummyControl::Step::End: GiveBack(adapter); break;
	case training::DummyControl::Step::None: break;
	}
}

// Each tick of a battle the pads play (mod::SetTrainingPadsHook): the lab's
// input in the two slots, from the update's first tick on.
void OnPads(mod::SfvAdapter& adapter, uint32_t* values, int slots)
{
	if (slots < 2) return;
	if (!s_lab.stepped) Step(adapter);
	if (s_lab.open && s_lab.plan.read) {
		if (!s_lab.prepared) {
			s_lab.prepared = true;
			training::Frame physical{};
			for (int side = 0; side < 2; side++) physical[side].mapped = physical[side].raw = lab::ToLab(values[side]);
			s_lab.output = s_lab.session.Prepare(physical);
			s_lab.commit = true;
		}
		if (s_lab.plan.override)
			for (int side = 0; side < 2; side++) values[side] = lab::ToSfv(s_lab.output[side].raw);
	}
	for (int side = 0; side < 2; side++) s_lab.recorded[side] = lab::ToLab(values[side]);
}

void Status(std::string& out)
{
	const training::View view = s_channel.Read();
	nlohmann::json status = {{"available", view.available}, {"ready", view.ready}, {"battle", view.generation},
		{"mode", view.mode == Mode::Idle ? "idle" : view.mode == Mode::Standby ? "standby" : view.mode == Mode::Recording ? "recording" : "playback"},
		{"selected", view.selected}, {"active", view.active}, {"cursor", view.cursor}, {"length", view.activeLength},
		{"side", view.playbackSide}, {"replying", view.replying}, {"loop", view.loop},
		{"order", view.order == training::Order::InOrder ? "inorder" : "random"}, {"x", {view.x[0], view.x[1]}}};
	nlohmann::json lengths = nlohmann::json::array(), on = nlohmann::json::array();
	for (int slot = 0; slot < training::SlotCount; slot++) {
		lengths.push_back(view.lengths[slot]);
		on.push_back(view.on[slot]);
	}
	status["lengths"] = lengths;
	status["on"] = on;
	status["dummy"] = {{"kind", s_told.kind.load()}, {"pad", s_told.pad.load()}, {"action", s_told.action.load()}};
	const auto seen = [](int frame) { return frame == training::ReplyTiming::NotSeen ? nlohmann::json() : nlohmann::json(frame); };
	status["reply"] = {{"timed", s_told.replies.load()}, {"sent", seen(s_told.sent)}, {"freed", seen(s_told.freed)},
		{"move", seen(s_told.move)}, {"timing", s_told.timing.load()}};
	out = status.dump();
}

} // namespace

void InstallSfvTrainingLab()
{
	mod::SetTrainingPadsHook(OnPads);
	// Out of the Training battle (back at the menu, or leaving for it) the lab
	// closes, keeping the dummy's slots for its next battle.
	mod::RunEachGameFrame([] {
		if (s_lab.open && !ReadTrainingView().available) Close(nullptr);
		// Out of the battle's tick, where SfvTraining leaves it for a call too.
		if (s_leave.exchange(false) && s_lab.open)
			mod::Log("training lab: told to leave for the main menu: %s", mod::LeaveBattle() ? "done" : "refused");
		return true;
	});
	mod::RegisterStatus("traininglab", [] {
		std::string out;
		Status(out);
		return out;
	});
}

training::View ReadTrainingLab()
{
	return s_channel.Read();
}

bool SubmitTrainingLab(const training::Command& command)
{
	return s_channel.Submit(command);
}

void RequestTrainingSetting(int index, int value)
{
	if (index >= 1 && index <= 13 && value >= 0 && value < 0x10000) s_setting = (index << 16) | value;
}

void RequestTrainingLeave()
{
	s_leave = true;
}

void SetTrainingLabControlsOpen(bool open)
{
	s_controlsOpen = open;
}

std::array<unsigned, 2> TrainingLabInputs()
{
	return s_lab.recorded;
}

void TrainingLabAfterUpdate(mod::SfvAdapter& adapter, const std::array<training::FighterSample, 2>& samples,
	const std::array<mod::FighterFrame, 2>& fighters, uint32_t ticks, const training::MeterView& meter)
{
	// An update that ran no tick (paused) still takes the controls' commands.
	if (!s_lab.stepped) Step(adapter);
	s_lab.stepped = s_lab.prepared = false;
	const bool commit = s_lab.commit;
	s_lab.commit = false;
	if (!s_lab.open) return;
	auto& session = s_lab.session;
	// The input read in this update is the next simulated one's to act on.
	if (ticks > 0) s_lab.control.Simulated();
	const bool read = fighters[0].valid && fighters[1].valid;
	if (s_lab.plan.sample && ticks == 1 && read) {
		// The dummy as its stun is timed by the game's own count, for a
		// reply's wait now and its decision below.
		const training::DummySeen seen = s_replies.Watch(samples[1], samples[0].action, lab::ScriptedFrames(fighters[1]));
		// A playback's waits read the fight: its fighter free, a hit on the other.
		const int side = session.GetView().playbackSide;
		const auto& own = samples[side];
		const auto& other = samples[1 - side];
		const auto& was = s_lab.before[1 - side];
		const bool hit = other.valid && ((training::ClassifyStatus(other.status) == training::Phase::Hit &&
			training::ClassifyStatus(was.status) != training::Phase::Hit) || other.comboDamage > was.comboDamage);
		const int until = own.valid && own.untilActionable >= 0 && own.untilActionable != training::UntimedFrames ? own.untilActionable : -1;
		session.Observe(own.valid && training::ClassifyStatus(own.status) == training::Phase::Neutral, hit, until,
			training::ReplyLab::Due(session, seen), false);
		s_lab.before = samples;
		// Recorded for the facing the input was prepared with, then the new facing.
		if (commit) session.Commit(s_lab.output);
		session.SetPositions(fighters[0].x, fighters[1].x);
		session.SetFacing(fighters[0].facingRight);
		const training::ReplyStep step = s_replies.Decide(seen, session);
		if (step.none) mod::Log("training lab: no reply, no recording that is on can be one");
		if (step.started) mod::Log("training lab: reply begins (the dummy %s %d frames)", seen.held ? "free in" : "free for",
			seen.held ? seen.until : seen.free);
		s_lab.frames++;
		if (TraceOn()) {
			const std::string line = training::ReplyFrameTrace(static_cast<int>(s_lab.frames), samples[1], seen, step,
				s_replies.Pending(), session.Replying());
			if (!line.empty()) mod::Log("training lab: reply trace %s", line.c_str());
		}
		// Where the reply's button met the dummy: a reversal's move starts on
		// the free frame, so both differences are the plan's timing.
		training::ReplyTiming timing;
		const bool pressed = commit && session.Replying() && (s_lab.output[1].raw & training::AttackButtons);
		if (s_replies.Trace(session, pressed, seen, samples[1].valid && samples[1].status == training::ActorStatus::Skill, timing)) {
			s_told.sent = timing.sent;
			s_told.freed = timing.freed;
			s_told.move = timing.move;
			s_told.timing = timing.timing;
			s_told.replies++;
			mod::Log("training lab: reply timing, frames after the reply was decided: button sent %s, dummy free %s, in its move "
					 "%s; timing %d; move %s after free, %s after the button was acted on",
				training::SeenAt(timing.sent).c_str(), training::SeenAt(timing.freed).c_str(), training::SeenAt(timing.move).c_str(),
				timing.timing, training::FramesBetween(timing.freed, timing.move).c_str(),
				training::FramesBetween(timing.Acted(), timing.move).c_str());
		}
	} else if (s_lab.plan.sample && ticks > 1) {
		// Frames the lab did not see: what the stun watch saw goes, and a
		// reply waiting on it.
		s_lab.gaps++;
		s_replies.Forget(session);
	}
	LogPlaying();
	Publish(&meter);
}

} // namespace sfve::adapter
