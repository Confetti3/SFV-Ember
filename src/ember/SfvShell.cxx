#include "ember/SfvShell.hxx"

#include "ember/EmberHost.hxx"
#include "ember/RoomBattle.hxx"
#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvHud.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvReplays.hxx"
#include "ember/SfvSelectionScreen.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"
#include "overlay/Overlay.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "common/MenuInputCapture.hxx"
#include "common/NativeMenuLock.hxx"
#include "common/PadKind.hxx"
#include "common/ReplayTransport.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "sf4e/sf4e__ShellHost.hxx"
#include "ui/ControllerNavigation.hxx"
#include "ui/GameMenu.hxx"
#include "ui/MenuRows.hxx"
#include "ui/OverlayPresentation.hxx"
#include "ui/ReplayPlaybackHud.hxx"
#include "ui/SelectionLabels.hxx"
#include "ui/Theme.hxx"
#include "ui/Win32Input.hxx"

#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h>

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
		view.colorSteps = ColourIds(pick.pick.fighter, pick.pick.costume, true).size() > 1;
		// SFV's fighters have no Ultra (the catalog's one choice) and no fighter
		// options (SF4's personal action and win quote): the table page shows
		// neither row.
		view.fighterOptions = false;
		if (snapshot.atMainMenu && !available) view.selectionError = loc::T("runtime.selection_unavailable");
	}

	// The table page's Appearance row steps the colour, through the costume's
	// own colours this player can ready; there is no Ultra.
	bool StepSelection(const sf4e::ui::ShellAction::SelectionStep& step, const RuntimeSnapshot& snapshot) override
	{
		if (!snapshot.canEditSelection || step.field != sf4e::ui::ShellAction::SelectionStep::Field::Color) return false;
		PlayerPick pick = CurrentPick();
		sf4e::ui::Step(pick.pick.color, ColourIds(pick.pick.fighter, pick.pick.costume, true), step.delta);
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
	bool started = false, wasShown = false, matchResultShown = false;
	// When the shell last showed or hid (ImGui's clock), for its fade.
	double changedAt = -1;
	// A replay's playback (EMBER_LIBRARY.md section 26): the Replays
	// screen's counts last seen, and when the strip was last asked for (a
	// control taken, or the playback starting).
	std::uint64_t logOpensSeen = 0, returnsSeen = 0;
	std::uint32_t takenSeen = 0, sessionSeen = 0;
	double takenAt = -1;
};

// Ember's shell over SFV fades in and out, the ground and all, rather than
// cut between the game and the shell's flat ground (EMBER_LIBRARY.md section
// 20): every SFV Ember screen stands on that ground, the post-match prompt
// too (ShellTheme::promptOnGround), so a fade is all a change of state shows.
// Opening, the shell and its ground come up together (FadeScope); closing,
// the ground goes down over the game it uncovers.
constexpr double kFadeSeconds = 0.2;

// 0 to 1 over the fade, eased out.
float FadeProgress(double since)
{
	if (since < 0 || since >= kFadeSeconds) return 1.f;
	const float x = static_cast<float>(since / kFadeSeconds);
	return 1.f - (1.f - x) * (1.f - x);
}

// What the shell draws in one frame, faded as a whole: its windows (every
// one it begins, its popups and tooltips too) and what it adds to ImGui's
// background and foreground lists (the prompt's ground). The shell's design
// parts draw in their own colours, which ImGui's style alpha would not reach,
// so the opacity goes on the vertices once they are drawn.
class FadeScope {
public:
	FadeScope()
		: firstWindow_(ImGui::GetCurrentContext()->WindowsActiveCount),
		  background_(ImGui::GetBackgroundDrawList()), backgroundFrom_(background_->VtxBuffer.Size),
		  foreground_(ImGui::GetForegroundDrawList()), foregroundFrom_(foreground_->VtxBuffer.Size)
	{
	}
	void Apply(float opacity) const
	{
		if (opacity >= 1.f) return;
		const ImGuiContext& g = *ImGui::GetCurrentContext();
		for (ImGuiWindow* window : g.Windows) {
			if (window->LastFrameActive == g.FrameCount && window->BeginOrderWithinContext >= firstWindow_)
				Scale(*window->DrawList, 0, opacity);
		}
		Scale(*background_, backgroundFrom_, opacity);
		Scale(*foreground_, foregroundFrom_, opacity);
	}

private:
	static void Scale(ImDrawList& list, int from, float opacity)
	{
		for (int i = from; i < list.VtxBuffer.Size; ++i) {
			ImU32& colour = list.VtxBuffer[i].col;
			const float alpha = static_cast<float>((colour & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT);
			colour = (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha * opacity + .5f) << IM_COL32_A_SHIFT);
		}
	}
	int firstWindow_;
	ImDrawList* background_;
	int backgroundFrom_;
	ImDrawList* foreground_;
	int foregroundFrom_;
};

Shell& TheShell()
{
	static Shell shell;
	return shell;
}

// A replay's playback (EMBER_LIBRARY.md section 26): its keys, read as SF4's
// overlay reads them (F1 pause, F2 step, F3 slower, F4 faster), and Esc to
// stop watching; then the library's strip, shown for a while after each
// control and as the playback starts, and while paused or slower. SFV has no
// frame meter or input lanes over a replay.
void DrawReplayControls(Shell& shell, const Snapshot& snapshot, bool shellShown)
{
	namespace rt = sf4e::replaytransport;
	const rt::View& playback = snapshot.replays.playback;
	const double now = ImGui::GetTime();
	if (!playback.playback) {
		shell.takenAt = -1;
		return;
	}
	if (playback.session != shell.sessionSeen || playback.taken != shell.takenSeen) shell.takenAt = now;
	shell.sessionSeen = playback.session;
	shell.takenSeen = playback.taken;
	if (shellShown) return;
	const ImGuiIO& io = ImGui::GetIO();
	if (overlay::GameWindowFocused() && !io.WantTextInput && !io.KeyAlt) {
		const auto send = [&](rt::Command command) { SubmitPlaybackCommand(command, rt::Device::Keyboard, playback.session); };
		if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) send(rt::Command::TogglePause);
		// Held, F2 steps again after 300 ms, ten times a second.
		for (int steps = ImGui::GetKeyPressedAmount(ImGuiKey_F2, .3f, .1f); steps > 0; --steps) send(rt::Command::Step);
		if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) send(rt::Command::Slower);
		if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) send(rt::Command::Faster);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) SubmitPlaybackStop(playback.session);
	}
	sf4e::ui::ReplayHudView view;
	view.shown = true;
	view.armed = playback.armed;
	view.paused = playback.paused;
	view.divisor = playback.divisor;
	view.round = playback.round;
	view.cursor = playback.cursor;
	view.pad = playback.device == rt::Device::Pad;
	view.stripAlpha = rt::StripAlpha(playback.paused, playback.divisor, shell.takenAt < 0 ? -1 : now - shell.takenAt);
	view.meterOffered = false;
	view.stopOffered = true;
	sf4e::ui::DrawReplayStrip(view);
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
	// Watch now: Ember's menu gets out of the way as the replay's battle is
	// launched, and comes back on the Replays screen when the main menu does
	// (SfvReplays), as SF4's overlay does around the game's battle log.
	if (snapshot.replays.logOpens != shell.logOpensSeen) {
		shell.logOpensSeen = snapshot.replays.logOpens;
		presentation.Close("a replay started");
	}
	if (snapshot.replays.returns != shell.returnsSeen) {
		shell.returnsSeen = snapshot.replays.returns;
		presentation.Open("the replay ended");
		shell.host.Shell().ShowReplays();
	}
	// The WIN of a room game this PC fought: the post-match prompt opens over
	// it, once per game, as SF4's overlay opens it (RoomBattle holds the WIN).
	const bool matchResult = snapshot.menuContext == input::MenuContext::MatchResult;
	if (matchResult && !shell.matchResultShown) presentation.Open("the match result");
	shell.matchResultShown = matchResult;
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
	DrawReplayControls(shell, snapshot, presentation.Visible());
	const double now = ImGui::GetTime();
	if (presentation.Visible() != shell.wasShown) shell.changedAt = now;
	const float fade = FadeProgress(now - shell.changedAt);
	if (presentation.Visible()) {
		const FadeScope scope;
		const char* close = shell.host.Draw(snapshot, shell.frame->netplay, shell.navigation, shell.game, {});
		scope.Apply(fade);
		if (close) presentation.Close(close);
	} else {
		shell.host.Background(snapshot);
		// The shell's ground going, over the game it uncovers.
		if (fade < 1.f) {
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos,
				ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
				sf4e::ui::palette::InkAt(static_cast<int>(sf4e::ui::WindowGroundAlpha * 255 * (1.f - fade) + .5f)));
		}
	}
	s_menuPads.Publish(presentation.Visible(), shell.openRequest);
	// Back to room or Character select on the prompt: the held WIN's decline.
	NoteShellLeftPrompt(shell.host.Shell().PostMatchLeft());
	if (presentation.Visible() != shell.wasShown) {
		shell.wasShown = presentation.Visible();
		shell.changedAt = now; // a close from the shell's own Draw too
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
	// A replay's battle plays the recording's inputs, and SFV's own pause
	// menu has no place over it: every pad is the playback's controls
	// (SfvReplays reads them from VirtualPads' record).
	if (PlaybackRunning()) return true;
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
	// During a replay's playback its keys are Ember's (F1 to F4, Esc), kept
	// from the game as SF4's window procedure keeps them (KeepsKey).
	ui.takesKey = [](unsigned message, unsigned key) {
		return PlaybackRunning() && (message == WM_KEYDOWN || message == WM_KEYUP) &&
			((key >= VK_F1 && key <= VK_F4) || key == VK_ESCAPE);
	};
	overlay::SetOverlayUi(std::move(ui));
}

} // namespace sfve::adapter
