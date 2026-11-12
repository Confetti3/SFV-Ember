#include "ember/SfvFrameMeter.hxx"

#include "ember/SfvHitboxes.hxx"
#include "ember/SfvMeterSample.hxx"
#include "ember/SfvTraining.hxx"
#include "mod/FighterReader.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/SfvAdapter.hxx"

#include "training/FrameMeter.hxx"
#include "ui/FrameMeterWindow.hxx"
#include "ui/TrainingHudLayout.hxx"

#include <imgui.h>

#include <windows.h>

#include <array>
#include <memory>
#include <mutex>

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;

// The log of what the meter measured, the meter's own evidence: each move
// once it is over (its startup, active and recovery as the bars count them),
// each exchange's advantage with Training's own beside it, and a state kind
// not seen in game, once a battle per fighter.
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

// Game thread only: the Training battle the meter follows, its fighters, the
// meter and its feed.
struct Followed {
	uint64_t battle = 0;
	mod::FighterReader reader;
	training::FrameMeter meter;
	MeterFeed feed;
	MeterLog log;
	bool readFailed = false;
};
std::unique_ptr<Followed> s_followed;

// What the render thread draws: the meter as the last tick left it, or
// nothing outside a followed Training battle.
std::mutex s_mutex;
std::shared_ptr<const training::MeterView> s_shown;

void Show(std::shared_ptr<const training::MeterView> view)
{
	std::lock_guard lock(s_mutex);
	s_shown = std::move(view);
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
		const mod::FighterFrame& fighter = fighters[side];
		if (fighter.valid && !MeterStatus(fighter.stateKind, fighter.posture) && !unnamed_[side]) {
			unnamed_[side] = true;
			mod::Log("frame meter: P%d is in state kind %d (script %d, posture %d), which was not seen in game before", side + 1,
				static_cast<int>(fighter.stateKind), fighter.script, fighter.posture);
		}
	}
	const auto& reading = view.advantage;
	if (reading.valid && !measured_)
		mod::Log("frame meter: P%d's %s: P1 %+d, P2 %+d (Training's own: P1 %+d, P2 %+d)%s", reading.attacker + 1,
			reading.blocked ? "block" : "hit", reading.frames[0], reading.frames[1], fighters[0].gameAdvantage,
			fighters[1].gameAdvantage, reading.knockdown ? ", knockdown" : "");
	measured_ = reading.valid;
}

// The battle SfvTraining follows, which the meter follows too: from the
// update after SfvTraining has seen it until it is no longer available.
std::unique_ptr<Followed> Follow(mod::SfvAdapter& adapter)
{
	auto followed = std::make_unique<Followed>();
	followed->battle = adapter.Battle();
	const bool bound = adapter.AnyUnit() &&
		followed->reader.Bind(adapter.AnyUnit(), reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
	mod::Log("frame meter: Training battle %llu %s", (unsigned long long)followed->battle,
		bound ? "followed" : "not followed (its fighters were not found)");
	return followed;
}

// After every battle frame update (mod::WatchBattleFrames).
void Watch(mod::SfvAdapter& adapter)
{
	const TrainingView training = ReadTrainingView();
	if (!training.available || training.generation != adapter.Battle()) {
		if (s_followed) {
			s_followed.reset();
			Show(nullptr);
			ForgetSfvHitboxes();
		}
		return;
	}
	if (!s_followed || s_followed->battle != adapter.Battle()) {
		s_followed = Follow(adapter);
		ForgetSfvHitboxes();
	}
	Followed& followed = *s_followed;
	const uint32_t ticks = adapter.TicksInLastUpdate();
	// Paused: no tick, nothing to measure.
	if (!followed.reader.Bound() || ticks == 0) return;
	std::array<mod::FighterFrame, 2> fighters;
	if (!followed.reader.Read(fighters) && !followed.readFailed) {
		followed.readFailed = true;
		mod::Log("frame meter: a fighter's state did not read; the meter shows the frame as unknown");
	}
	followed.feed.Feed(followed.meter, fighters, ticks);
	followed.log.Note(followed.meter.View(), fighters);
	Show(std::make_shared<const training::MeterView>(followed.meter.View()));
	// The hitbox viewer reads the same battle's boxes after the same tick
	// (one watcher: mod::WatchBattleFrames).
	ReadSfvHitboxes(followed.reader);
}

} // namespace

void InstallSfvFrameMeter()
{
	mod::WatchBattleFrames(Watch);
}

void DrawSfvFrameMeter()
{
	std::shared_ptr<const training::MeterView> view;
	{
		std::lock_guard lock(s_mutex);
		view = s_shown;
	}
	if (!view) return;
	// The Training HUD's own defaults (training::MeterOptions): angled bars,
	// 60 frames across, the numbers and the state line, the standard size.
	const training::MeterOptions options;
	const float hudScale = sf4e::ui::FrameMeterHudScale(options.size);
	const auto* viewport = ImGui::GetMainViewport();
	sf4e::ui::DrawFrameMeterWindow(*view, hudScale, sf4e::ui::TrainingMeterWidth(hudScale),
		viewport->Pos.y + viewport->Size.y * sf4e::ui::TrainingHudBottom, options);
}

} // namespace sfve::adapter
