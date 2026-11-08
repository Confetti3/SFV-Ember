// The crash record (mod/CrashRecord) and how the launcher's dump channel
// reaches it (-sfve_dump_channel). The record is written once per process,
// so one test takes the whole path: a real fault's pointers, the record, and
// the launcher's side of the channel (common/CrashDump.hxx's DumpChannel,
// served from a thread here) writing the dump the record asked for.

#include <doctest/doctest.h>

#include "mod/CrashRecord.hxx"
#include "mod/ModConfig.hxx"

#include "common/CrashDump.hxx"
#include "common/ProductIdentity.hxx"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using sfve::mod::Config;
using sfve::mod::LoadConfigFromCommandLine;

namespace {

EXCEPTION_RECORD s_record;
CONTEXT s_context;

int Keep(EXCEPTION_POINTERS* pointers)
{
	s_record = *pointers->ExceptionRecord;
	s_context = *pointers->ContextRecord;
	return EXCEPTION_EXECUTE_HANDLER;
}

// A real access violation's record and context, kept past the handler.
void Fault()
{
	__try {
		*reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x10)) = 1;
	} __except (Keep(GetExceptionInformation())) {
	}
}

std::string ReadAll(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	std::ostringstream text;
	text << file.rdbuf();
	return text.str();
}

} // namespace

TEST_CASE("the launcher's dump channel reaches the mod as three hex handle values")
{
	LoadConfigFromCommandLine(L"StreetFighterV.exe -sfve_mode=netplay -sfve_dump_channel=1a4:1b0:2c8");
	CHECK(Config().dumpChannel.Present());
	CHECK(Config().dumpChannel.request == 0x1a4);
	CHECK(Config().dumpChannel.done == 0x1b0);
	CHECK(Config().dumpChannel.mailbox == 0x2c8);
	for (const wchar_t* line : {L"x -sfve_dump_channel=1a4:1b0", L"x -sfve_dump_channel=1a4:0:2c8", L"x -sfve_dump_channel=1a4:1b0:2c8:9",
			 L"x -sfve_dump_channel=zz:1b0:2c8", L"x -sfve_dump_channel=-1:1b0:2c8", L"x -sfve_dump_channel=0x1a4:1b0:2c8",
			 L"x -sfve_dump_channel=1a4: 1b0:2c8", L"x"}) {
		LoadConfigFromCommandLine(line);
		CHECK_FALSE(Config().dumpChannel.Present());
	}
	CHECK_FALSE(Config().crashCommand);
	LoadConfigFromCommandLine(L"x -sfve_crash_command=1");
	CHECK(Config().crashCommand);
	LoadConfigFromCommandLine(L"");
}

TEST_CASE("a crash leaves the record and the dump the launcher wrote for it")
{
	wchar_t temp[MAX_PATH] = {};
	REQUIRE(GetTempPathW(MAX_PATH, temp));
	const std::filesystem::path logs =
		std::filesystem::path(temp) / (L"sfve-crash-record-test-" + std::to_wstring(GetCurrentProcessId()));
	std::filesystem::remove_all(logs);
	REQUIRE(std::filesystem::create_directories(logs));

	sf4e::crash::DumpChannel channel;
	REQUIRE(channel.Create());
	const sfve::mod::CrashRecordState state = sfve::mod::InstallCrashRecord(
		{logs.wstring(), {reinterpret_cast<uintptr_t>(channel.request), reinterpret_cast<uintptr_t>(channel.done),
							 reinterpret_cast<uintptr_t>(channel.mailbox)},
			false});
	CHECK(state.installed);
	CHECK(state.launcherChannel);
	CHECK_FALSE(state.dumpWriterHooked);

	// The launcher's side: one request, served as ServeUntilExit would.
	bool served = false;
	std::thread launcher([&] {
		if (WaitForSingleObject(channel.request, 30000) == WAIT_OBJECT_0) served = channel.Serve(GetCurrentProcess(), logs.c_str());
	});
	Fault();
	REQUIRE(s_record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION);
	EXCEPTION_POINTERS pointers = {&s_record, &s_context};
	sfve::mod::RecordCrash("unhandled_exception", &pointers, "the game's crash reporter", GetCurrentThreadId());
	launcher.join();
	CHECK(served);
	const std::wstring dump = channel.written;
	CHECK(dump.find(ember::Product().crashDumpPrefix) != std::wstring::npos);
	CHECK(std::filesystem::exists(dump));

	const std::string record = ReadAll(logs / ember::Product().crashLog);
	CHECK(record.rfind("kind=unhandled_exception code=0xC0000005", 0) == 0);
	CHECK(record.find("message=write of 0x0000000000000010") != std::string::npos);
	CHECK(record.find("module=sfve_ember_tests.exe+0x") != std::string::npos);

	// One record per process: a second crash changes nothing.
	sfve::mod::RecordCrash("terminate", nullptr, "std::terminate", GetCurrentThreadId());
	CHECK(ReadAll(logs / ember::Product().crashLog) == record);

	channel.Close();
	std::error_code ignored;
	std::filesystem::remove_all(logs, ignored);
}
