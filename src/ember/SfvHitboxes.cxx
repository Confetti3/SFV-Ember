#include "ember/SfvHitboxes.hxx"

#include "ember/SfvBoxes.hxx"
#include "mod/FighterReader.hxx"
#include "mod/Log.hxx"

#include "netplay/JsonFileStore.hxx"
#include "netplay/SettingsStore.hxx"
#include "training/PracticeSettings.hxx"
#include "ui/HitboxOverlay.hxx"

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;
namespace json_file = sf4e::netplay::json_file;

// SF4's lab keeps its choices in training.json beside its settings
// (TrainingPanel's PracticeFile); SFV Ember keeps the viewer's there too, in
// its own settings folder, as the meter options' "hitboxes".
constexpr const wchar_t* kPracticeFile = L"training.json";

std::filesystem::path Folder()
{
	return std::filesystem::path(sf4e::netplay::SettingsStore::DefaultDirectory());
}

// The choice and the file it is kept in: read once, written by the row.
std::mutex s_choiceMutex;
bool s_loaded = false;
std::atomic<bool> s_shown{false};

// training.json as it is now: its object, or an empty one when there is none
// or it does not read as one (logged; the next save writes a fresh one).
nlohmann::json ReadPractice()
{
	std::string bytes, error;
	bool missing = false;
	nlohmann::json practice;
	if (!json_file::ReadBytes(Folder() / kPracticeFile, bytes, missing, error) || missing) {
		if (!missing) mod::Log("hitboxes: training.json does not read (%s)", error.c_str());
		return nlohmann::json::object();
	}
	if (!json_file::Parse(bytes, practice, error) || !practice.is_object()) {
		mod::Log("hitboxes: training.json is no settings object (%s); the next save writes a new one", error.c_str());
		return nlohmann::json::object();
	}
	return practice;
}

training::MeterOptions MeterOf(const nlohmann::json& practice)
{
	const auto meter = practice.find("meter");
	return training::ReadMeterOptions(meter != practice.end() ? *meter : nlohmann::json::object());
}

void LoadLocked()
{
	if (s_loaded) return;
	s_loaded = true;
	s_shown = MeterOf(ReadPractice()).hitboxes;
	mod::Log("hitboxes: %s (training.json)", s_shown ? "on" : "off");
}

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
	bool first = true, readFailed = false, noCamera = false, stray = false, otherShapes = false;
	unsigned undrawnTypes = 0;
};
BattleNotes s_notes;

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
}

} // namespace

void ReadSfvHitboxes(const mod::FighterReader& reader)
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

bool SfvHitboxesShown()
{
	{
		std::lock_guard lock(s_choiceMutex);
		LoadLocked();
	}
	return s_shown;
}

bool ShowSfvHitboxes(bool shown)
{
	std::lock_guard lock(s_choiceMutex);
	LoadLocked();
	// The file's other choices stay as they are; the meter's options are
	// written back whole, as SF4's lab writes them (MeterOptionsJson).
	nlohmann::json practice = ReadPractice();
	training::MeterOptions options = MeterOf(practice);
	options.hitboxes = shown;
	practice["meter"] = training::MeterOptionsJson(options);
	std::string error;
	if (!json_file::Publish(Folder(), kPracticeFile, practice, error)) {
		mod::Log("hitboxes: the choice could not be saved (%s); it stays %s", error.c_str(), s_shown ? "on" : "off");
		return false;
	}
	s_shown = shown;
	mod::Log("hitboxes: %s, saved", shown ? "on" : "off");
	return true;
}

} // namespace sfve::adapter
