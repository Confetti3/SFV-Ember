#include "ember/EmberHost.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameExit.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"
#include "platform/HelperProcess.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;

enum class HostState { WaitingForMenu, WaitingForContent, Running, Refused, Stopped };

std::atomic<HostState> s_state{HostState::WaitingForMenu};
std::string s_refusal; // why the runtime did not start; written before Refused is published

const char* StateName(HostState state)
{
	switch (state) {
	case HostState::WaitingForMenu: return "waiting for the main menu";
	case HostState::WaitingForContent: return "waiting for the game's content lists";
	case HostState::Running: return "running";
	case HostState::Refused: return "refused";
	case HostState::Stopped: return "stopped";
	}
	return "?";
}

// The runtime's spdlog lines, in the mod's log.
void RouteRuntimeLog()
{
	auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg& message) {
		const spdlog::string_view_t level = spdlog::level::to_string_view(message.level);
		mod::Log("ember %.*s: %.*s", (int)level.size(), level.data(), (int)message.payload.size(), message.payload.data());
	});
	auto logger = std::make_shared<spdlog::logger>("ember", std::move(sink));
	logger->set_level(spdlog::level::info);
	spdlog::set_default_logger(std::move(logger));
}

sf4e::netplay::RoomState RoomNow()
{
	return facade::GetRuntimeSnapshotShared()->session.room;
}

// The game is exiting (GameExit, on the game thread, every thread still
// running). First the player leaves the room as Leave room does: the room
// hears the Leave and acknowledges it, so the other members see this copy
// leave (a host's room goes on without it) rather than lose it and recover.
// GameExit's steps tick the runtime until the room is left or its time is
// up, then the engine's exit goes on.
void BeginLeaveAtExit()
{
	if (RoomNow() == sf4e::netplay::RoomState::Idle) return;
	facade::RuntimeCommand leave;
	leave.command.kind = sf4e::netplay::CommandKind::LeaveRoom;
	leave.command.generation = facade::GetRuntimeSnapshotShared()->session.generation;
	const bool submitted = facade::SubmitRuntimeCommand(std::move(leave));
	mod::Log("ember: leaving the room for the game's exit%s", submitted ? "" : " (the runtime took no command)");
}

// Then, as SF4's fMain::Destroy does: the session ends, the helper stops
// (StopHelper gives a room still open a bounded departure of its own), and
// the settings still queued are written. The tick stops first: StopHelper
// deletes the runtime it reads.
void StopAtExit()
{
	const bool left = RoomNow() == sf4e::netplay::RoomState::Idle;
	s_state = HostState::Stopped;
	facade::ShutdownNetplay(true);
	facade::StopHelper();
	StopPreferences();
	mod::Log("ember: %s, stopped the helper and saved the settings for the game's exit",
		left ? "left the room" : "the room was not left in time");
}

bool Refuse(const std::string& why)
{
	s_refusal = why;
	s_state = HostState::Refused;
	mod::Log("ember: the runtime does not start: %s", why.c_str());
	return false;
}

// What this copy's player may not pick, as the catalog was filled with it
// (EMBER_LIBRARY.md section 23): the game's answer and -sfve_unowned's marks.
void LogOwnership(const mod::ContentLists& lists, bool gate)
{
	int fighters = 0, costumes = 0, colours = 0;
	for (const mod::FighterContent& fighter : lists.fighters) {
		fighters += !fighter.owned;
		for (const mod::CostumeContent& costume : fighter.costumes) {
			costumes += !costume.owned;
			colours += (int)(costume.colours.size() - costume.ownedColours.size());
		}
	}
	const mod::OwnershipSource& source = lists.ownership;
	mod::Log("ember: ownership gate %s; the game's setup restricted %d (%d entries), filter %d, logged in %d; "
		"%d marked unowned by -sfve_unowned; not owned: %d fighters, %d costumes, %d colours, %zu of %zu stages",
		gate ? "on" : "off (development)", source.restricted, source.restrictionEntries, source.filter, source.loggedIn,
		source.markedUnowned, fighters, costumes, colours, lists.stages.size() - lists.ownedStages.size(), lists.stages.size());
}

// The first main-menu frames: the catalog, then the helper and the runtime.
// False once the runtime will not start.
bool TryStart(const HostSettings& settings, ember::GameTick& battle)
{
	const ULONGLONG readStarted = GetTickCount64();
	mod::ContentLists lists = mod::ReadContentLists();
	mod::MarkUnowned(lists, settings.unowned);
	if (lists.fighters.empty() || lists.stages.empty()) {
		if (s_state != HostState::WaitingForContent) mod::Log("ember: waiting for the game's content lists");
		s_state = HostState::WaitingForContent;
		return true;
	}
	if (!FillCatalog(lists, settings.ownershipGate) || !ember::Usable(ember::Catalog())) {
		return Refuse("the game's lists (" + std::to_string(lists.fighters.size()) + " fighters, " +
			std::to_string(lists.stages.size()) + " stages) do not make a usable catalog");
	}
	SealCatalog();
	const size_t named = std::count_if(lists.stageNames.begin(), lists.stageNames.end(), [](const std::string& n) { return !n.empty(); });
	mod::Log("ember: catalog of %d fighters and %zu stages (%zu named by the game; read in %llu ms), build %s",
		ember::Catalog().fighterCount, lists.stages.size(), named, GetTickCount64() - readStarted, MatchBuildId().c_str());
	LogOwnership(lists, settings.ownershipGate);

	RouteRuntimeLog();
	// Kept for the process's life: its job ends the helper with the process.
	auto* helper = new sf4e::platform::HelperProcess();
	const std::wstring exe = mod::ModuleFolder() + L"\\sf4-net.exe";
	if (helper->Start(exe, GetCurrentProcessId())) {
		facade::ConfigureHelper(helper->Bootstrap(), 0);
	} else {
		// The runtime starts anyway and shows the helper as unavailable.
		mod::Log("ember: could not start sf4-net.exe (error %lu)", helper->LastError());
		facade::ConfigureHelper({}, helper->LastError());
	}
	sf4e::NetplayConfig config;
	const std::string name = "SFV " + std::to_string(settings.instance + 1);
	strncpy_s(config.displayName, name.c_str(), _TRUNCATE);
	config.editionSelect = 0; // SFV has no editions
	facade::InitFromPayload(config);
	facade::StartHelper();
	facade::NotifyRuntimeGameReady();
	// The main menu is in front: the game's screens are up.
	facade::NotifyRuntimeEventSystemReady();
	// The game's frames stop once its window is closed, so the exit's steps
	// tick the runtime themselves while the Leave goes out.
	mod::OnGameExit({&BeginLeaveAtExit,
		[&battle] {
			ember::Tick(battle);
			return RoomNow() == sf4e::netplay::RoomState::Idle;
		},
		&StopAtExit});
	s_state = HostState::Running;
	mod::Log("ember: the runtime runs as \"%s\"", name.c_str());
	return true;
}

} // namespace

bool EmberRuntimeRunning()
{
	return s_state == HostState::Running;
}

void InstallEmberHost(const HostSettings& settings, ember::GameTick& battle)
{
	mod::RegisterStatus("ember", [] {
		const HostState state = s_state;
		std::string json = "{\"host\":" + mod::JsonString(StateName(state));
		if (state == HostState::Refused) json += ",\"refusal\":" + mod::JsonString(s_refusal);
		return json + "}";
	});
	mod::RunEachGameFrame([settings, &battle] {
		if (s_state == HostState::Stopped) return false;
		if (s_state != HostState::Running) {
			if (!mod::MainMenuInFront()) return true;
			if (!TryStart(settings, battle)) return false;
			if (s_state != HostState::Running) return true;
		}
		ember::Tick(battle);
		return true;
	});
}

} // namespace sfve::adapter
