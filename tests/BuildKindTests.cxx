// What a player build takes and refuses (mod/BuildKind.hxx; EMBER_LIBRARY.md
// section 42): the mod reads only its launcher's options, the control pipe
// answers ping and status alone. Both kinds are checked here whatever this
// test program was built as. And where the mod's log goes when its usual
// file cannot be written (mod/Log.hxx).

#include <doctest/doctest.h>

#include "mod/ControlPipe.hxx"
#include "mod/LogFile.hxx"
#include "mod/ModConfig.hxx"

#include <windows.h>

#include <filesystem>
#include <string>

using sfve::BuildKind;
using sfve::mod::Config;
using sfve::mod::LoadConfigFromCommandLine;

namespace {

// Every test option, and the launcher's own.
constexpr const wchar_t* kTestLine =
	L"StreetFighterV.exe -sfve_instance=1 -sfve_ownership=0 -sfve_mash=7 -sfve_mash_pad=1 -sfve_perturb=1 -sfve_lag=60 "
	L"-sfve_crash_command=1 -sfve_unowned=RYU -sfve_uninstalled=S01 -sfve_pads=0,1 -sfve_background=1 -sfve_port=7100 "
	L"-sfve_grant_all=0 -sfve_moveset=cody -sfve_idle=1 -sfve_tiny=2 -sfve_socket_log=1 -sfve_replay_any_build=1 "
	L"-sfve_loader=launcher -sfve_mode=netplay -sfve_room=1 -sfve_dump_channel=1a4:1b0:2c8";

} // namespace

TEST_CASE("a player build reads only its launcher's options")
{
	LoadConfigFromCommandLine(kTestLine, BuildKind::Player);
	const sfve::mod::ModConfig& player = Config();
	CHECK(player.mode == sfve::mod::ModMode::Netplay);
	CHECK(player.room);
	CHECK(player.dumpChannel.Present());
	CHECK(player.instance == 0);
	CHECK(player.ownershipGate == sfve::mod::kOwnershipGateByDefault);
	CHECK(player.grantAll); // a player build's game always grants everything
	CHECK(player.script.seed == 0);
	CHECK_FALSE(player.script.padBefore);
	CHECK_FALSE(player.perturb);
	CHECK(player.lagMs == 0);
	CHECK_FALSE(player.crashCommand);
	CHECK(player.unowned.empty());
	CHECK(player.uninstalled.empty());
	CHECK(player.virtualPadMask == 0);
	CHECK_FALSE(player.backgroundPlay);
	CHECK(player.port == 7000);
	CHECK(player.script.moveSet == sfve::mod::input::MoveSet::System);
	CHECK(player.script.idle == -1);
	CHECK(player.testTiny == 0);
	CHECK_FALSE(player.socketLog);
	CHECK_FALSE(player.replayAnyBuild);
	const std::string& ignored = sfve::mod::PlayerBuildIgnored();
	for (const char* option :
		{"-sfve_instance", "-sfve_ownership", "-sfve_mash", "-sfve_lag", "-sfve_crash_command", "-sfve_pads", "-sfve_grant_all",
			"-sfve_moveset", "-sfve_idle", "-sfve_tiny", "-sfve_socket_log", "-sfve_replay_any_build"}) {
		CHECK(ignored.find(option) != std::string::npos);
	}
	CHECK(ignored.find("-sfve_mode") == std::string::npos);
	CHECK(ignored.find("-sfve_loader") == std::string::npos);

	LoadConfigFromCommandLine(kTestLine, BuildKind::Development);
	const sfve::mod::ModConfig& development = Config();
	CHECK(development.instance == 1);
	CHECK_FALSE(development.ownershipGate);
	CHECK_FALSE(development.grantAll);
	CHECK(development.script.seed == 7);
	CHECK(development.lagMs == 60);
	CHECK(development.crashCommand);
	CHECK(development.unowned.size() == 1);
	CHECK(development.virtualPadMask == 3);
	CHECK(development.port == 7100);
	// A move set other than the system moves plays moves.
	CHECK(development.script.script == sfve::mod::input::Script::Moves);
	CHECK(development.script.moveSet == sfve::mod::input::MoveSet::Cody);
	CHECK(development.script.idle == 1);
	CHECK(development.testTiny == 2);
	CHECK(development.socketLog);
	CHECK(development.replayAnyBuild);
	CHECK(sfve::mod::PlayerBuildIgnored().empty());

	// The launcher's mode is netplay; any other is a test's.
	LoadConfigFromCommandLine(L"x -sfve_mode=synctest -sfve_perturb=1", BuildKind::Player);
	CHECK(Config().mode == sfve::mod::ModMode::Probe);
	CHECK(sfve::mod::PlayerBuildIgnored().find("-sfve_mode") != std::string::npos);
	LoadConfigFromCommandLine(L"x -sfve_mode=synctest", BuildKind::Development);
	CHECK(Config().mode == sfve::mod::ModMode::SyncTest);
	LoadConfigFromCommandLine(L"");
}

TEST_CASE("the photo booth is a development build's mode only")
{
	// A player build reads -sfve_mode only as netplay: booth is a test's
	// option, ignored and noted, and the mode stays the default.
	LoadConfigFromCommandLine(L"x -sfve_mode=booth", BuildKind::Player);
	CHECK(Config().mode == sfve::mod::ModMode::Probe);
	CHECK(sfve::mod::PlayerBuildIgnored().find("-sfve_mode") != std::string::npos);
	LoadConfigFromCommandLine(L"x -sfve_mode=booth -sfve_room=1", BuildKind::Player);
	CHECK(Config().mode == sfve::mod::ModMode::Probe);
	// A development build makes it, in any case; rooms are netplay's only.
	LoadConfigFromCommandLine(L"x -sfve_mode=booth", BuildKind::Development);
	CHECK(Config().mode == sfve::mod::ModMode::Booth);
	CHECK(std::string(sfve::mod::ModeName(Config().mode)) == "booth");
	LoadConfigFromCommandLine(L"x -sfve_mode=BOOTH -sfve_room=1", BuildKind::Development);
	CHECK(Config().mode == sfve::mod::ModMode::Booth);
	CHECK_FALSE(Config().room);
	CHECK(sfve::mod::IgnoredSetting() != nullptr);
	LoadConfigFromCommandLine(L"");
}

TEST_CASE("a player build's pipe answers ping and status only")
{
	bool ran = false;
	sfve::mod::RegisterCommand("buildkind-test", [&ran](const std::string&) {
		ran = true;
		return std::string("ok ran");
	});
	CHECK(sfve::mod::Answer("ping", BuildKind::Player) == "ok pong");
	CHECK(sfve::mod::Answer("status", BuildKind::Player).rfind("ok {", 0) == 0);
	for (const char* line : {"buildkind-test", "pad 0 a", "peek x", "catalog-export C:\\x.json", "quit", "trust y"}) {
		CHECK(sfve::mod::Answer(line, BuildKind::Player).rfind("error ", 0) == 0);
	}
	CHECK_FALSE(ran);
	CHECK(sfve::mod::Answer("buildkind-test", BuildKind::Development) == "ok ran");
	CHECK(ran);
}

TEST_CASE("a log another program holds is not lost: the process logs to a file of its own")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("sfve-log-test-" + std::to_string(GetCurrentProcessId()));
	std::filesystem::remove_all(folder);
	const std::wstring usual = (folder / "logs" / "sfv_ember.log").wstring();
	const std::wstring own = (folder / "logs" / "sfv_ember-1.log").wstring();
	const std::wstring elsewhere = (folder / "local" / "sfv_ember-1.log").wstring();

	// Free: the usual file, created with its folder.
	sfve::mod::OpenedLog first = sfve::mod::OpenFirstLog({{usual, false}, {own, true}});
	REQUIRE(first.file != INVALID_HANDLE_VALUE);
	CHECK(first.path == usual);
	CHECK(first.error == 0);
	CloseHandle(first.file);

	// Held by a reader that shares no writing, as a log viewer may.
	const HANDLE holder = CreateFileW(usual.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	REQUIRE(holder != INVALID_HANDLE_VALUE);
	sfve::mod::OpenedLog held = sfve::mod::OpenFirstLog({{usual, false}, {own, true}, {elsewhere, true}});
	REQUIRE(held.file != INVALID_HANDLE_VALUE);
	CHECK(held.path == own);
	CHECK(held.failedPath == usual);
	CHECK(held.error == ERROR_SHARING_VIOLATION);
	// A file of its own is never another process's: an existing one is passed by.
	sfve::mod::OpenedLog next = sfve::mod::OpenFirstLog({{usual, false}, {own, true}, {elsewhere, true}});
	REQUIRE(next.file != INVALID_HANDLE_VALUE);
	CHECK(next.path == elsewhere);
	CHECK(next.failedPath == usual);
	CloseHandle(next.file);
	CloseHandle(held.file);
	CloseHandle(holder);

	// Nowhere to write: no file, and the first failure kept.
	sfve::mod::OpenedLog none = sfve::mod::OpenFirstLog({{L"", false}, {(folder / "logs").wstring(), true}});
	CHECK(none.file == INVALID_HANDLE_VALUE);
	CHECK(none.failedPath == (folder / "logs").wstring());
	CHECK(none.error != 0);
	std::filesystem::remove_all(folder);
}

TEST_CASE("the log's candidates: the usual file of the instance, then the process's own, under three folders")
{
	using sfve::mod::LogCandidate;
	const auto candidates = sfve::mod::LogCandidates(0, 4242, L"C:\\Roam\\SFVEmber\\logs", L"C:\\Loc\\SFVEmber\\logs", L"C:\\Tmp\\");
	REQUIRE(candidates.size() == 4);
	CHECK(candidates[0].path == L"C:\\Roam\\SFVEmber\\logs\\sfv_ember.log");
	CHECK_FALSE(candidates[0].own);
	CHECK(candidates[1].path == L"C:\\Roam\\SFVEmber\\logs\\sfv_ember-4242.log");
	CHECK(candidates[2].path == L"C:\\Loc\\SFVEmber\\logs\\sfv_ember-4242.log");
	CHECK(candidates[3].path == L"C:\\Tmp\\sfv_ember-4242.log"); // %TEMP% ends in a backslash already
	for (size_t i = 1; i < candidates.size(); i++) CHECK(candidates[i].own);

	// A second game on the PC (instance 2) has a usual file of its own.
	CHECK(sfve::mod::LogCandidates(2, 7, L"C:\\L", L"", L"")[0].path == L"C:\\L\\sfv_ember.2.log");

	// A folder Windows would not name is skipped, never turned into a path
	// relative to the game's folder.
	const auto none = sfve::mod::LogCandidates(0, 7, L"", L"", L"");
	REQUIRE(none.size() == 4);
	for (const LogCandidate& candidate : none) CHECK(candidate.path.empty());
	CHECK(sfve::mod::LogCandidates(0, 7, L"", L"C:\\Loc", L"")[2].path == L"C:\\Loc\\sfv_ember-7.log");
}
