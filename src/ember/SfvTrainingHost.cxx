#include "ember/SfvTrainingHost.hxx"

#include "ember/SfvFrameMeter.hxx"
#include "ember/SfvHitboxes.hxx"
#include "ember/SfvTraining.hxx"
#include "ember/SfvTrainingLab.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"

#include "common/PadKind.hxx"
#include "common/TrainingPad.hxx"
#include "netplay/SettingsStore.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"
#include "training/PracticeSettings.hxx"
#include "ui/GameMenu.hxx"
#include "ui/TrainingPanel.hxx"

#include <imgui.h>

#include <nlohmann/json.hpp>

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;
namespace ui = sf4e::ui;

// The controls' one controller (the library's): the render thread opens and
// closes them, the pad's chord on the game thread asks, and the window
// procedure ends the focus period.
sf4e::input::TrainingControls s_controls;
// Game thread: the chord's gesture with its owner.
sf4e::input::TrainingPadInput s_pad;
// Render thread: the HUD shown (F5), and the controls open last frame.
bool s_hud = true;
bool s_wasOpen = false;
// The pipe's requests, carried out on the render thread in order.
std::mutex s_requestMutex;
std::vector<std::string> s_requests;
// What the pipe's status says of the controls (render thread writes).
std::atomic<bool> s_hudShown{true};
std::mutex s_statusMutex;
std::string s_screen;

// Named apart from sf4e::training::Submit (SF4's runtime), which a call with a
// training::Command would also find by argument-dependent lookup.
bool SubmitToLab(training::Command command)
{
	return SubmitTrainingLab(command);
}

// One pipe request (see InstallSfvTrainingHost), on the render thread.
void Carry(const std::string& line, const training::View& view)
{
	std::istringstream words(line);
	std::string verb, a, b;
	words >> verb >> a >> b;
	const auto command = [&](training::Action action) {
		training::Command c;
		c.action = action;
		c.generation = view.generation;
		return c;
	};
	const auto on = [](const std::string& word) { return word == "on" || word == "1"; };
	bool done = true;
	if (verb == "record") ui::TrainingRecordPress(view, SubmitToLab, s_controls.Sample());
	else if (verb == "play") ui::TrainingPlayPress(view, SubmitToLab);
	else if (verb == "stop") done = SubmitToLab(command(training::Action::Interrupt));
	else if (verb == "select") {
		auto c = command(training::Action::Select);
		c.slot = std::atoi(a.c_str()) - 1;
		done = SubmitToLab(c);
	} else if (verb == "slot") {
		auto c = command(training::Action::SlotOn);
		c.slot = std::atoi(a.c_str()) - 1;
		c.on = on(b);
		done = SubmitToLab(c);
	} else if (verb == "loop") {
		auto c = command(training::Action::Loop);
		c.loop = on(a);
		done = SubmitToLab(c);
	} else if (verb == "order") {
		auto c = command(training::Action::PlayOrder);
		c.order = a == "inorder" ? training::Order::InOrder : training::Order::Random;
		done = SubmitToLab(c);
	} else if (verb == "clear") done = SubmitToLab(command(training::Action::Clear));
	else if (verb == "mirror") done = SubmitToLab(command(training::Action::Mirror));
	else if (verb == "save") done = ui::SaveTrainingRecording(view, SubmitToLab, a);
	else if (verb == "load") done = ui::LoadTrainingRecording(view, SubmitToLab, a);
	else if (verb == "reply") {
		// reply off|hit|block|rise|any [timing N] [chance N]
		auto lab = ui::TrainingLabState();
		const char* const whens[] = {"off", "hit", "block", "rise", "any"};
		for (int when = 0; when < 5; when++)
			if (a == whens[when]) lab.plan.when = when;
		std::string key = b, value;
		while (!key.empty()) {
			words >> value;
			if (key == "timing") lab.plan.timing = std::atoi(value.c_str());
			else if (key == "chance") lab.plan.chance = std::atoi(value.c_str());
			key.clear();
			words >> key;
		}
		ui::SaveTrainingLab();
		done = ui::SendTrainingPlan(view, SubmitToLab);
	} else if (verb == "meter") {
		// meter <training.json's meter key> <JSON value>, as the pages set it
		nlohmann::json meter = training::MeterOptionsJson(ui::TrainingMeterOptions());
		const auto value = nlohmann::json::parse(b, nullptr, false);
		meter[a] = value.is_discarded() ? nlohmann::json(b) : value;
		ui::SetTrainingMeterOptions(training::ReadMeterOptions(meter));
		ui::SaveTrainingLab();
	} else if (verb == "controls") {
		if (a == "open") done = s_controls.Open(s_controls.Sample());
		else s_controls.Close();
	} else if (verb == "page") {
		ui::TrainingNavigation().Home();
		if (a != "home") ui::TrainingNavigation().Push(a);
	} else if (verb == "hud") s_hud = on(a);
	else if (verb == "setting") RequestTrainingSetting(std::atoi(a.c_str()), std::atoi(b.c_str()));
	else if (verb == "leave") RequestTrainingLeave();
	else done = false;
	mod::Log("training host: pipe \"%s\" %s", line.c_str(), done ? "done" : "refused");
}

std::string Requested(const std::string& args)
{
	std::lock_guard lock(s_requestMutex);
	s_requests.push_back(args);
	return "ok queued";
}

} // namespace

void InstallSfvTrainingHost()
{
	// The lab's settings and named recordings in SFV Ember's settings folder,
	// as SF4 Ember keeps them in its own (training.json, recordings).
	ui::SetTrainingDirectory(sf4e::netplay::SettingsStore::DefaultDirectory());
	ui::SetTrainingFeatures(ember::game::Game().caps.trainingFeatures);
	// traininglab record|play|stop|select N|slot N on|off|loop on|off|order
	// random|inorder|clear|mirror|save NAME|load NAME|reply WHEN [timing N]
	// [chance N]|meter KEY VALUE|controls open|close|page ID|hud on|off|
	// setting INDEX VALUE|leave (one of Training's own settings and leaving
	// for the main menu, for the tests)
	mod::RegisterCommand("traininglab", Requested);
	mod::RegisterStatus("traininghost", [] {
		std::string screen;
		{
			std::lock_guard lock(s_statusMutex);
			screen = s_screen;
		}
		return "{\"controls\":" + std::string(s_controls.Open() ? "true" : "false") + ",\"hud\":" +
			(s_hudShown ? "true" : "false") + ",\"screen\":" + mod::JsonString(screen) + "}";
	});
}

bool TrainingControlsOpen()
{
	return s_controls.Open();
}

void TrainingFocus(bool focused)
{
	if (focused) s_controls.GainFocus();
	else s_controls.LoseFocus();
}

bool TrainingTakesPad(unsigned short xinputButtons, bool focused)
{
	const TrainingView battle = ReadTrainingView();
	if (!battle.available || !focused) {
		s_pad.Reset();
		return false;
	}
	const training::View lab = ReadTrainingLab();
	const float place[2] = {lab.x[0], lab.x[1]};
	const auto result = s_pad.Update(sf4e::input::xinput::Physical(xinputButtons), s_controls, GetTickCount64() / 1000.0,
		battle.leavingIn > 0 ? sf4e::input::TrainingCall::Called : sf4e::input::TrainingCall::None,
		{battle.generation, s_controls.Epoch()}, place, training::ChordStartsRecording(lab.mode), TrainingPaused());
	if (result.events.record) {
		sf4e::input::TrainingPadEvent event;
		event.kind = sf4e::input::TrainingPadEvent::Kind::Record;
		event.generation = result.owner.generation;
		event.epoch = result.owner.epoch;
		s_controls.Post(event);
	}
	// The controls hold the pad while open, as Ember's menu does; the
	// chord's Start never reaches the game's pause.
	return s_controls.Open() || (result.events.owned & sf4e::input::PhysicalStart) != 0;
}

bool TrainingHitboxesChosen()
{
	return ui::TrainingMeterOptions().hitboxes;
}

bool ChooseTrainingHitboxes(bool shown)
{
	training::MeterOptions options = ui::TrainingMeterOptions();
	options.hitboxes = shown;
	ui::SetTrainingMeterOptions(options);
	ui::SaveTrainingLab();
	return true;
}

void DrawSfvTrainingLab(bool available, bool shellShown, bool paused, bool focused, bool called,
	const sf4e::ui::TrainingRoomStatus& room)
{
	const training::View view = ReadTrainingLab();
	const bool lab = available && view.available;
	if (!lab || !focused || called || shellShown) s_controls.Close();
	// What the game thread reads of the lab's options: the meter's history
	// and after-pause, and the boxes while the HUD shows them.
	const training::MeterOptions options = ui::TrainingMeterOptions();
	SetSfvMeterHistory(options.history, options.continuous);
	SetSfvHitboxesShown(lab && options.hitboxes && s_hud);
	{
		std::vector<std::string> requests;
		{
			std::lock_guard lock(s_requestMutex);
			requests.swap(s_requests);
		}
		for (const auto& line : requests) {
			if (lab) Carry(line, view);
			else mod::Log("training host: pipe \"%s\" refused (no Training battle)", line.c_str());
		}
	}
	const auto pads = s_controls.Take();
	if (!lab) {
		SetTrainingLabControlsOpen(false);
		s_wasOpen = false;
		return;
	}
	ui::ServeTrainingRecordings(view);
	const ImGuiIO& io = ImGui::GetIO();
	if (focused && !shellShown) {
		for (const auto& pad : pads)
			if (pad.generation == view.generation && s_controls.Current(pad)) ui::ApplyTrainingPadEvent(view, SubmitToLab, s_controls, pad, called);
		// F7 on a recorded slot: the session asked about overwriting it.
		if (sf4e::input::TrainingControls::Token asked; ui::TakeOverwriteAsk(view, asked) && !called) ui::OpenTrainingRecordings(s_controls, asked);
		if (!io.WantTextInput && !io.KeyAlt) {
			if (ImGui::IsKeyPressed(ImGuiKey_F6, false) && !called) s_controls.Toggle(s_controls.Sample());
			if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) s_hud = !s_hud;
		}
		// The dummy's plan each battle (the position keys are not SFV's).
		// F10 goes back to the room in a room's Training, not under the call.
		ui::TrainingHotkeys(view, SubmitToLab, room.InRoom() && !called);
		if (ui::TrainingPressesAct(s_controls, called) && !io.WantTextInput && !io.KeyAlt) {
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_F1 + ui::TrainingRecordKey), false))
				ui::TrainingRecordPress(view, SubmitToLab, s_controls.Sample());
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_F1 + ui::TrainingPlayKey), false)) ui::TrainingPlayPress(view, SubmitToLab);
		}
	}
	const bool open = s_controls.Open();
	// The controls opening interrupt standby, a recording or a playback, once per opening.
	if (open && !s_wasOpen) SubmitToLab({training::Action::Interrupt, view.generation});
	s_wasOpen = open;
	SetTrainingLabControlsOpen(open);
	s_hudShown = s_hud;
	// The boxes behind every window, then the HUD, never over the game's pause.
	if (!paused && s_hud) DrawSfvHitboxes();
	if (!shellShown && !paused && s_hud) ui::DrawTrainingHud(view, called, room.InRoom());
	if (open) {
		ui::TakeForwardedMenuAction();
		ui::DrawTrainingFlyout(view, SubmitToLab, room);
		if (ui::TakeForwardedMenuAction().kind == ui::MenuAction::Close) s_controls.Close();
	}
	std::lock_guard lock(s_statusMutex);
	s_screen = open ? ui::TrainingNavigation().Screen() : std::string();
}

} // namespace sfve::adapter
