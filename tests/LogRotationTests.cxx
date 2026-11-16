#include "mod/LogFile.hxx"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path FreshFolder(const char* name)
{
	const fs::path folder = fs::temp_directory_path() / name;
	fs::remove_all(folder);
	fs::create_directories(folder);
	return folder;
}

void Put(const fs::path& file, const std::string& text)
{
	std::ofstream(file, std::ios::binary) << text;
}

std::string Get(const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), {});
}

// `file` last written `secondsAgo` before `now`.
void Age(const fs::path& file, FILETIME now, int secondsAgo)
{
	ULARGE_INTEGER t;
	t.LowPart = now.dwLowDateTime;
	t.HighPart = now.dwHighDateTime;
	t.QuadPart = (ULONGLONG)((LONGLONG)t.QuadPart - (LONGLONG)secondsAgo * 10000000ll);
	const FILETIME at = {t.LowPart, t.HighPart};
	const HANDLE handle = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(handle != INVALID_HANDLE_VALUE);
	SetFileTime(handle, nullptr, nullptr, &at);
	CloseHandle(handle);
}

} // namespace

TEST_CASE("an earlier run's log is moved to .old1 and the older ones shift, the last of the kept runs dropped")
{
	const fs::path folder = FreshFolder("sfve-log-rotation");
	FILETIME now;
	GetSystemTimeAsFileTime(&now);
	const fs::path log = folder / "sfv_ember.log";
	Put(log, "run 4");
	Put(folder / "sfv_ember.old1.log", "run 3");
	Put(folder / "sfv_ember.old2.log", "run 2");
	Put(folder / "sfv_ember.old3.log", "run 1");
	Age(log, now, 60);

	REQUIRE(sfve::mod::RotateLog(log.wstring(), 3, now));
	CHECK_FALSE(fs::exists(log));
	CHECK(Get(folder / "sfv_ember.old1.log") == "run 4");
	CHECK(Get(folder / "sfv_ember.old2.log") == "run 3");
	CHECK(Get(folder / "sfv_ember.old3.log") == "run 2"); // run 1 is gone
	CHECK_FALSE(fs::exists(folder / "sfv_ember.old4.log"));

	// The second game's log (instance 2) has its own run.
	const fs::path second = folder / "sfv_ember.2.log";
	Put(second, "second game");
	Age(second, now, 60);
	REQUIRE(sfve::mod::RotateLog(second.wstring(), 3, now));
	CHECK(Get(folder / "sfv_ember.2.old1.log") == "second game");
	fs::remove_all(folder);
}

TEST_CASE("a log this run has written, an empty one and a missing one stay where they are")
{
	const fs::path folder = FreshFolder("sfve-log-rotation-stay");
	FILETIME now;
	GetSystemTimeAsFileTime(&now);
	const fs::path log = folder / "sfv_ember.log";
	CHECK_FALSE(sfve::mod::RotateLog(log.wstring(), 5, now));
	Put(log, "");
	Age(log, now, 60);
	CHECK_FALSE(sfve::mod::RotateLog(log.wstring(), 5, now));
	Put(log, "written after this process started: another copy's or a start line");
	Age(log, now, -5);
	CHECK_FALSE(sfve::mod::RotateLog(log.wstring(), 5, now));
	CHECK(fs::exists(log));
	CHECK_FALSE(fs::exists(folder / "sfv_ember.old1.log"));

	// Held by a program that allows no rename (a viewer): the log is appended to as before.
	Age(log, now, 60);
	const HANDLE holder = CreateFileW(log.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	REQUIRE(holder != INVALID_HANDLE_VALUE);
	CHECK_FALSE(sfve::mod::RotateLog(log.wstring(), 5, now));
	CloseHandle(holder);
	fs::remove_all(folder);
}

TEST_CASE("one run's log has a size limit: the line past it, a notice, then nothing")
{
	using Take = sfve::mod::RunLogCap::Take;
	sfve::mod::RunLogCap cap(100);
	CHECK(cap.Admit(60) == Take::Write);
	CHECK(cap.Admit(40) == Take::Write); // exactly the limit
	CHECK(cap.Admit(1) == Take::WriteThenNotice);
	CHECK(cap.Admit(1) == Take::Drop);
	CHECK(cap.Admit(0) == Take::Drop);
}
