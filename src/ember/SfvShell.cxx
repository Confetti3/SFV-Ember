#include "ember/SfvShell.hxx"

#include "ember/EmberHost.hxx"
#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvHud.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvSelectionScreen.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"
#include "overlay/Overlay.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "common/MenuInputCapture.hxx"
#include "common/NativeMenuLock.hxx"
#include "common/PadKind.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "sf4e/sf4e__ShellHost.hxx"
#include "ui/ControllerNavigation.hxx"
#include "ui/GameMenu.hxx"
#include "ui/MenuRows.hxx"
#include "ui/OverlayPresentation.hxx"
#include "ui/SelectionLabels.hxx"
#include "ui/Theme.hxx"
#include "ui/Win32Input.hxx"

#include <imgui.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;
namespace input = sf4e::input;
namespace loc = sf4e::loc;
using Snapshot = facade::RuntimeSnapshot;

// The pads between the game thread, which asks for opens and says where the
// game is, and the render thread, which answers them (OverlayPresentation.hxx).
sf4e::ui::MenuPads s_menuPads;

// The game's own menu takes no input while a room is joined or a game is on
// its way, whether or not Ember is shown: the only way on from it is Ember's.
bool NativeMenuLocked(const Snapshot& snapshot)
{
	return snapshot.menuContext == input::MenuContext::MainMenu &&
		input::NativeMenuLockFor(snapshot.session.room, snapshot.session.match) != input::NativeMenuLock::None;
}

// Whether the pick can be readied, by SFV's catalog.
bool PickAvailable(const PlayerPick& pick, const Snapshot& snapshot)
{
	const ember::GameCatalog& catalog = ember::Catalog();
	return ember::ValidFighter(pick.pick.fighter) &&
		catalog.availablePick(pick.pick, false, snapshot.fighterAvailability[(size_t)pick.pick.fighter],
			snapshot.installedContent.get());
}

// SFV's part of the shell's frame: the pick (SfvPick) and its screen.
class SfvShellGame final : public ember::ShellGame {
public:
	void FillView(const RuntimeSnapshot& snapshot, sf4e::ui::ShellView& view) override
	{
		const PlayerPick pick = CurrentPick();
		const bool available = PickAvailable(pick, snapshot);
		view.canReady = view.canReady && snapshot.atMainMenu && available;
		view.build = MatchBuildId();
		view.selectedPick = pick.pick;
		view.fighterName = ember::Catalog().fighterName(pick.pick.fighter);
		view.appearanceName = loc::Tf("selection.appearance_value", sf4e::ui::CostumeLabel(pick.pick), ColourName(pick.pick.color));
		view.selectionSummary = view.fighterName + " / " + view.appearanceName;
		view.stageName = sf4e::ui::StageLabel(pick.stage);
		view.colorSteps = ColourIds(pick.pick.fighter, pick.pick.costume).size() > 1;
		// SFV's fighters have no Ultra (the catalog's one choice) and no fighter
		// options (SF4's personal action and win quote): the table page shows
		// neither row.
		view.fighterOptions = false;
		if (snapshot.atMainMenu && !available) view.selectionError = loc::T("runtime.selection_unavailable");
	}

	// The table page's Appearance row steps the colour, through the costume's
	// own colours; there is no Ultra.
	bool StepSelection(const sf4e::ui::ShellAction::SelectionStep& step, const RuntimeSnapshot& snapshot) override
	{
		if (!snapshot.canEditSelection || step.field != sf4e::ui::ShellAction::SelectionStep::Field::Color) return false;
		PlayerPick pick = CurrentPick();
		sf4e::ui::Step(pick.pick.color, ColourIds(pick.pick.fighter, pick.pick.costume), step.delta);
		SetPick(pick);
		return true;
	}

	// SFV offers no Training (the adapter's capability is off), so the host
	// never calls this.
	void StartTraining(const RuntimeSnapshot&) override { mod::Log("shell: Training asked for, but SFV Ember has none"); }

	void AddPick(facade::RuntimeCommand& command) override { AddPickTo(command); }

	// The stage is this player's to choose out of a room, or as P1 in one.
	void DrawSelection(bool, const RuntimeSnapshot& snapshot) override
	{
		const bool stage = snapshot.session.room == sf4e::netplay::RoomState::Idle || snapshot.localSlot == 0;
		const std::string error =
			PickAvailable(CurrentPick(), snapshot) ? std::string() : loc::T("runtime.selection_combination_unavailable");
		screen_.Draw(snapshot.canEditSelection, stage, error);
	}

private:
	SfvSelectionScreen screen_;
};

// What the "shell" status field reports, as the render thread last left it.
struct ShellStatus {
	bool shown = false;
	std::string why, screen, focus;
};
std::mutex s_statusMutex;
ShellStatus s_status;

// The shell and what it keeps between frames. Render thread only.
struct Shell {
	sf4e::ui::OverlayPresentation presentation;
	sf4e::ui::ControllerNavigation navigation;
	ember::ShellHost host;
	SfvShellGame game;
	// The frame being drawn: the runtime's latest presentation, read once.
	std::shared_ptr<const facade::PresentationSnapshot> frame;
	sf4e::ui::MenuPads::Taken openRequest;
	bool started = false, wasShown = false;
};

Shell& TheShell()
{
	static Shell shell;
	return shell;
}

// Render thread, before the frame: the fonts at the picture's scale and the
// player's interface size.
bool ApplyScale(float pictureScale)
{
	float interfaceScale = 1.f;
	if (EmberRuntimeRunning()) {
		const auto& preferences = facade::GetRuntimeSnapshotShared()->preferences;
		interfaceScale = preferences.interfaceScale * (preferences.tvMode ? 1.25f : 1.f);
	}
	return sf4e::ui::ApplyTheme(pictureScale, interfaceScale);
}

// Render thread, before ImGui's frame: where the game is, the opens the pads
// asked for, and the pad's menu buttons.
void BeginFrame()
{
	Shell& shell = TheShell();
	if (!EmberRuntimeRunning()) {
		shell.frame.reset();
		return;
	}
	shell.frame = facade::GetPresentationSnapshotShared();
	const Snapshot& snapshot = *shell.frame->runtime;
	const bool focused = overlay::GameWindowFocused();
	auto& presentation = shell.presentation;
	// SFV's own menu comes first: Ember shows once the player opens it.
	if (!shell.started) {
		shell.started = true;
		presentation.Close("Start or F10 opens Ember");
	}
	presentation.Update(snapshot.menuContext, snapshot.session.match, snapshot.offlineRequested, focused);
	shell.openRequest = s_menuPads.Take();
	if (shell.openRequest.kind == sf4e::ui::MenuPads::Kind::Controls) {
		switch (input::AnswerStart(presentation.Visible(), snapshot.menuContext, shell.host.Shell().TextEntryOpen())) {
		case input::StartAnswer::Open: presentation.Open("Start"); break;
		case input::StartAnswer::Close: presentation.Close("Start"); break;
		case input::StartAnswer::None: break;
		}
	}
	sf4e::ui::SetOverlayCursorOwnership(focused && presentation.Visible());
	// The shell navigates by its own rows, not ImGui's spatial navigation.
	ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
	shell.navigation.Update(snapshot.menuController, input::ControllerMenuAvailable(snapshot.menuContext),
		presentation.Visible(), snapshot.inputCapture == input::Capture::Idle);
}

void PublishStatus(Shell& shell)
{
	ShellStatus status;
	status.shown = shell.presentation.Visible();
	status.why = shell.presentation.Why();
	sf4e::ui::MenuNavigation& navigation = shell.host.Shell().Navigation();
	status.screen = navigation.Screen();
	status.focus = navigation.Focus();
	std::lock_guard lock(s_statusMutex);
	s_status = std::move(status);
}

// Render thread, inside ImGui's frame: the shell shown, or its hidden work.
overlay::OverlayUi::Shown Draw()
{
	Shell& shell = TheShell();
	if (!shell.frame) return {};
	const Snapshot& snapshot = *shell.frame->runtime;
	auto& presentation = shell.presentation;
	if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) presentation.Toggle("F10");
	sf4e::ui::SetMenuInput({shell.navigation.Buttons(), ImGui::GetTime()});
	sf4e::ui::SetMenuGlyphs(snapshot.menuController.deviceType, snapshot.menuController.menu, snapshot.promptBrand);
	if (presentation.Reopened()) shell.host.Shell().ShowPlay();
	// Under the shell: the match HUD, or only its notices while the shell is open.
	DrawSfvMatchHud(*shell.frame, presentation.Visible());
	if (presentation.Visible()) {
		if (const char* close = shell.host.Draw(snapshot, shell.frame->netplay, shell.navigation, shell.game, {}))
			presentation.Close(close);
	} else {
		shell.host.Background(snapshot);
	}
	s_menuPads.Publish(presentation.Visible(), shell.openRequest);
	if (presentation.Visible() != shell.wasShown) {
		shell.wasShown = presentation.Visible();
		mod::Log("shell: %s (%s), room %s, match %s", shell.wasShown ? "shown" : "hidden", presentation.Why(),
			input::RoomStateName(snapshot.session.room), input::MatchStateName(snapshot.session.match));
	}
	PublishStatus(shell);
	return {presentation.Visible(), presentation.Available()};
}

// Game thread, for each pad state the game reads: Start on the gameplay pad
// asks for the shell, and the shell keeps the pads while it holds them or the
// game's menu is locked.
bool TakesPad(unsigned long slot, unsigned short buttons)
{
	if (!EmberRuntimeRunning()) return false;
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	const input::Device& device = snapshot->inputDevice;
	if (device.type == input::PadXInput && device.index == (int)slot) {
		// Where the game is, once per pad update: the gameplay pad's read.
		s_menuPads.Place(snapshot->menuContext, sf4e::netplay::Fighting(snapshot->session.match));
		static input::StartPress start;
		const bool allowed = input::ControllerOpensMenu(snapshot->menuContext) && snapshot->inputCapture == input::Capture::Idle;
		if (start.Update(allowed, input::xinput::Physical(buttons)) &&
			(overlay::GameWindowFocused() || s_menuPads.StartOpens())) {
			s_menuPads.Request(sf4e::ui::MenuPads::Kind::Controls);
		}
	}
	return s_menuPads.Holds() || NativeMenuLocked(*snapshot);
}

std::string StatusJson()
{
	std::lock_guard lock(s_statusMutex);
	return "{\"shown\":" + std::string(s_status.shown ? "true" : "false") + ",\"why\":" + mod::JsonString(s_status.why) +
		",\"screen\":" + mod::JsonString(s_status.screen) + ",\"focus\":" + mod::JsonString(s_status.focus) + "}";
}

} // namespace

bool ShellTakesPads()
{
	return s_menuPads.Takes();
}

void InstallEmberShell()
{
	InstallSfvArt();
	InstallSfvHud();
	mod::RegisterStatus("shell", StatusJson);
	overlay::OverlayUi ui;
	ui.applyScale = ApplyScale;
	ui.beginFrame = BeginFrame;
	ui.draw = Draw;
	ui.takesPad = TakesPad;
	overlay::SetOverlayUi(std::move(ui));
}

} // namespace sfve::adapter
