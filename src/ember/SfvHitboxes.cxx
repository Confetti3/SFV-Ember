#include "ember/SfvHitboxes.hxx"

#include "ember/SfvBoxes.hxx"
#include "mod/FighterReader.hxx"
#include "mod/Log.hxx"

#include "ui/HitboxOverlay.hxx"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;

// The player's choice as the lab's owner of training.json keeps it
// (ui::TrainingMeterOptions().hitboxes), handed over by SfvTrainingHost each
// frame with the HUD shown.
std::atomic<bool> s_shown{false};

// What the render thread draws: the boxes as the last tick left them, or
// nothing outside a followed Training battle.
std::mutex s_viewMutex;
std::shared_ptr<const training::BoxView> s_view;

void Show(std::shared_ptr<const training::BoxView> view)
{
	std::lock_guard lock(s_viewMutex);
	s_view = std::move(view);
}

// Game thread only: what the log has said of this battle, so a run can tell
// what the viewer met without a line per tick.
struct BattleNotes {
	bool first = true, readFailed = false, noCamera = false, stray = false, otherShapes = false, empty = false;
	unsigned undrawnTypes = 0;
	// The attack records seen, by type, flags and whether a box was made.
	std::set<std::tuple<int, uint32_t, bool>> attacks;
};
BattleNotes s_notes;

// Each kind of attack record once a battle: its type, its flags, the size of
// its rectangle, and the scripts both fighters played when it was first met.
// A box that never shows (a command grab's, in the 0.1.0 field reports) shows
// here as a record of some type and flags that was or was not drawn, or not at
// all, and the next run says which.
void NoteAttackRecords(const BoxTally& tally, const std::array<mod::FighterFrame, 2>& fighters)
{
	for (const AttackRecord& record : tally.attacks)
		if (s_notes.attacks.emplace(record.type, record.flags, record.drawn).second)
			mod::Log("hitboxes: P%d's %s attack record, type %d, flags 0x%08X, %.2f x %.2f, %s (scripts: P1 %d, P2 %d)", record.side + 1,
				record.projectile ? "projectile's" : "own", record.type, record.flags, record.width, record.height,
				record.drawn ? "drawn" : "not drawn", fighters[0].script, fighters[1].script);
}

void Note(const mod::BoxFrame& frame, const training::BoxView& view, const BoxTally& tally)
{
	if (s_notes.first) {
		s_notes.first = false;
		int boxes[2] = {}, projectiles = 0;
		for (const auto& box : view.boxes) boxes[box.side & 1]++;
		for (const auto& part : frame.parts) projectiles += part.projectile;
		const mod::CameraFrame& camera = frame.camera;
		const float* m = camera.fightToWorld.data();
		mod::Log("hitboxes: first tick: P1 %d boxes, P2 %d, %d projectiles out; camera %s: eye (%.1f, %.1f, %.1f), target (%.1f, "
				 "%.1f, %.1f), field of view %.2f; fight frame rows (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f "
				 "%.3f) (%.3f %.3f %.3f %.3f)",
			boxes[0], boxes[1], projectiles, view.valid ? "used" : "not usable", camera.eye[0], camera.eye[1], camera.eye[2],
			camera.target[0], camera.target[1], camera.target[2], camera.fieldOfView, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
			m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
	}
	if (!view.valid && !s_notes.noCamera) {
		s_notes.noCamera = true;
		mod::Log("hitboxes: the camera did not read or is no camera; boxes are not drawn until it is");
	}
	if (const unsigned fresh = tally.undrawnTypes & ~s_notes.undrawnTypes) {
		s_notes.undrawnTypes |= fresh;
		for (int type = 0; type < mod::sfv::boxes::kAttackTypes; type++)
			if (fresh & (1u << type)) mod::Log("hitboxes: an attack box of type %d is listed (not drawn: no attack's reach)", type);
	}
	if (tally.stray && !s_notes.stray) {
		s_notes.stray = true;
		mod::Log("hitboxes: %d list entries point at no record of their block (left out)", tally.stray);
	}
	if (tally.otherShapes && !s_notes.otherShapes) {
		s_notes.otherShapes = true;
		mod::Log("hitboxes: %d listed records are not rectangles (left out)", tally.otherShapes);
	}
	if (tally.empty && !s_notes.empty) {
		s_notes.empty = true;
		mod::Log("hitboxes: %d listed records have no area or no number (left out)", tally.empty);
	}
}

} // namespace

void ReadSfvHitboxes(const mod::FighterReader& reader, const std::array<mod::FighterFrame, 2>& fighters)
{
	if (!SfvHitboxesShown()) {
		Show(nullptr);
		return;
	}
	mod::BoxFrame frame;
	if (!reader.ReadBoxes(frame)) {
		if (!s_notes.readFailed) mod::Log("hitboxes: a fighter's boxes did not read; none are drawn for that tick");
		s_notes.readFailed = true;
		Show(nullptr);
		return;
	}
	BoxTally tally;
	auto view = std::make_shared<training::BoxView>(SfvBoxView(frame, tally));
	Note(frame, *view, tally);
	NoteAttackRecords(tally, fighters);
	Show(std::move(view));
}

void ForgetSfvHitboxes()
{
	Show(nullptr);
	s_notes = {};
}

void DrawSfvHitboxes()
{
	if (!SfvHitboxesShown()) return;
	std::shared_ptr<const training::BoxView> view;
	{
		std::lock_guard lock(s_viewMutex);
		view = s_view;
	}
	// SFV's view has no skeleton, and its hurt boxes are all shown: the
	// parts SF4's lab may hide are SF4's page's.
	if (view) sf4e::ui::DrawHitboxOverlay(*view, sf4e::ui::HitboxOverlayParts{});
}

void SetSfvHitboxesShown(bool shown)
{
	if (s_shown.exchange(shown) != shown) mod::Log("hitboxes: %s", shown ? "on" : "off");
}

bool SfvHitboxesShown()
{
	return s_shown;
}

} // namespace sfve::adapter
