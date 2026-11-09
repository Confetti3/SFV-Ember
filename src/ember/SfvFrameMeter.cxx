#include "ember/SfvFrameMeter.hxx"

#include "ember/SfvMeterSample.hxx"
#include "mod/FighterReader.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/SfvAdapter.hxx"
#include "mod/SfvOffsets.hxx"

#include "training/FrameMeter.hxx"
#include "ui/FrameMeterWindow.hxx"
#include "ui/TrainingHudLayout.hxx"

#include <imgui.h>

#include <windows.h>

#include <array>
#include <mutex>

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;

// The meter and whether a Training battle's ticks feed it: written on the
// game thread after each tick, drawn on the render thread.
std::mutex s_mutex;
training::FrameMeter s_meter;
bool s_live = false;

// The log of what the meter measured, the meter's own evidence: each move
// once it is over (its startup, active and recovery as the bars count them),
// each exchange's advantage with Training's own beside it, and a state kind
// section 39 does not name, once a battle per fighter.
class MeterLog {
public:
	void Note(const training::MeterView& view, const std::array<mod::FighterFrame, 2>& fighters);

private:
	// Each fighter's move while it lasts, and the script that started it.
	std::array<bool, 2> moving_{};
	std::array<int, 2> moveScript_{{-1, -1}};
	std::array<bool, 2> unnamed_{};
	bool measured_ = false;
};

// Game thread only: the Training battle followed and its meter's feed.
struct Followed {
	uint64_t battle = 0;
	mod::FighterReader reader;
	std::array<MeterMove, 2> moves{};
	int frame = 0;
	bool readFailed = false;
	MeterLog log;
};
Followed s_followed;

bool TrainingInFront()
{
	return mod::SceneStateKind(mod::CurrentSceneState()) == mod::sfv::scene::kTrainingBattleVtable &&
		!mod::SceneChangePending();
}

// A battle starts or stops feeding the meter: either way it starts over.
void SetLive(bool live)
{
	std::lock_guard lock(s_mutex);
	s_live = live;
	s_meter.Reset();
}

void Follow(mod::SfvAdapter& adapter)
{
	s_followed = {};
	s_followed.battle = adapter.Battle();
	const bool bound = adapter.AnyUnit() &&
		s_followed.reader.Bind(adapter.AnyUnit(), reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
	mod::Log("frame meter: Training battle %llu %s", (unsigned long long)s_followed.battle,
		bound ? "followed" : "not followed (its fighters were not found)");
	SetLive(bound);
}

void MeterLog::Note(const training::MeterView& view, const std::array<mod::FighterFrame, 2>& fighters)
{
	for (int side = 0; side < 2; ++side) {
		const auto& move = view.moves[side];
		if (move.live && !moving_[side]) moveScript_[side] = view.current[side].action;
		if (moving_[side] && !move.live && move.seen)
			mod::Log("frame meter: P%d's script %d: startup %d, active %d, recovery %d", side + 1, moveScript_[side],
				view.startupFrames[side], move.active, move.recovery);
		moving_[side] = move.live;
		const int kind = fighters[side].stateKind;
		if (fighters[side].valid && !MeterStatus(kind, fighters[side].posture) && !unnamed_[side]) {
			unnamed_[side] = true;
			mod::Log("frame meter: P%d is in state kind %d (action %d, posture %d), which section 39 does not name", side + 1,
				kind, fighters[side].script, fighters[side].posture);
		}
	}
	const auto& reading = view.advantage;
	if (reading.valid && !measured_) {
		const int attacker = reading.attacker;
		mod::Log("frame meter: P%d's %s: P1 %+d, P2 %+d (Training's own: P1 %+d, P2 %+d)%s", attacker + 1,
			reading.blocked ? "block" : "hit", reading.frames[0], reading.frames[1], fighters[0].gameAdvantage,
			fighters[1].gameAdvantage, reading.knockdown ? ", knockdown" : "");
	}
	measured_ = reading.valid;
}

// After every battle frame update (mod::WatchBattleFrames).
void Watch(mod::SfvAdapter& adapter)
{
	if (!TrainingInFront()) {
		if (s_followed.battle) {
			mod::Log("frame meter: Training battle %llu is no longer followed", (unsigned long long)s_followed.battle);
			s_followed = {};
			SetLive(false);
		}
		return;
	}
	if (adapter.Battle() != s_followed.battle) Follow(adapter);
	const uint32_t ticks = adapter.TicksInLastUpdate();
	// Paused: no tick, nothing to measure. More than one tick in an update
	// leaves a gap, and the meter starts over after it.
	if (!s_followed.reader.Bound() || ticks == 0) return;
	s_followed.frame += static_cast<int>(ticks);
	std::array<mod::FighterFrame, 2> fighters;
	if (!s_followed.reader.Read(fighters) && !s_followed.readFailed) {
		s_followed.readFailed = true;
		mod::Log("frame meter: a fighter's state did not read; the meter shows the frame as unknown");
	}
	const std::array<training::FighterSample, 2> samples{MeterSample(fighters[0], s_followed.moves[0]),
		MeterSample(fighters[1], s_followed.moves[1])};
	std::lock_guard lock(s_mutex);
	s_meter.Observe(s_followed.frame, samples);
	s_followed.log.Note(s_meter.View(), fighters);
}

} // namespace

void InstallSfvFrameMeter()
{
	mod::WatchBattleFrames(Watch);
}

void DrawSfvFrameMeter()
{
	std::lock_guard lock(s_mutex);
	if (!s_live) return;
	// The Training HUD's own defaults (training::MeterOptions): angled bars,
	// 60 frames across, the numbers and the state line, the standard size.
	const training::MeterOptions options;
	const float hudScale = sf4e::ui::FrameMeterHudScale(options.size);
	const auto* viewport = ImGui::GetMainViewport();
	sf4e::ui::DrawFrameMeterWindow(s_meter.View(), hudScale, sf4e::ui::TrainingMeterWidth(hudScale),
		viewport->Pos.y + viewport->Size.y * sf4e::ui::TrainingHudBottom, options);
}

} // namespace sfve::adapter
