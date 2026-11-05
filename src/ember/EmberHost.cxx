#include "ember/EmberHost.hxx"

#include "ember/SfvCatalog.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"
#include "platform/HelperProcess.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

#include <windows.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;

enum class HostState { WaitingForMenu, WaitingForContent, Running, Refused };

std::atomic<HostState> s_state{HostState::WaitingForMenu};
std::string s_refusal; // why the runtime did not start; written before Refused is published

const char* StateName(HostState state)
{
	switch (state) {
	case HostState::WaitingForMenu: return "waiting for the main menu";
	case HostState::WaitingForContent: return "waiting for the game's content lists";
	case HostState::Running: return "running";
	case HostState::Refused: return "refused";
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

// Ends the room link as the process goes, as SF4's Main::Destroy does,
// without StopHelper's waits: they would run under the loader lock, and the
// helper's job ends it with the process. Constructed once the runtime
// starts, after the battle mode, so it goes first.
struct RuntimeShutdown {
	~RuntimeShutdown() { facade::ShutdownNetplay(true); }
};

bool Refuse(const std::string& why)
{
	s_refusal = why;
	s_state = HostState::Refused;
	mod::Log("ember: the runtime does not start: %s", why.c_str());
	return false;
}

// The first main-menu frames: the catalog, then the helper and the runtime.
// False once the runtime will not start.
bool TryStart(const HostSettings& settings)
{
	const mod::ContentLists lists = mod::ReadContentLists();
	if (lists.fighters.empty() || lists.stages.empty()) {
		if (s_state != HostState::WaitingForContent) mod::Log("ember: waiting for the game's content lists");
		s_state = HostState::WaitingForContent;
		return true;
	}
	if (!FillCatalog(lists) || !ember::Usable(ember::Catalog())) {
		return Refuse("the game's lists (" + std::to_string(lists.fighters.size()) + " fighters, " +
			std::to_string(lists.stages.size()) + " stages) do not make a usable catalog");
	}
	SealCatalog();
	mod::Log("ember: catalog of %d fighters and %zu stages, build %s", ember::Catalog().fighterCount, lists.stages.size(),
		MatchBuildId().c_str());

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
	static RuntimeShutdown shutdown;
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
		if (s_state != HostState::Running) {
			if (!mod::MainMenuInFront()) return true;
			if (!TryStart(settings)) return false;
			if (s_state != HostState::Running) return true;
		}
		ember::Tick(battle);
		return true;
	});
}

} // namespace sfve::adapter
