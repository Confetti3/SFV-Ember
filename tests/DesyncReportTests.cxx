// A desync's report (ember/DesyncReport.hxx): what the comment Bugsink shows
// and groups by holds, the region lines both players' reports list, the
// mod's log tail before the desync, the report's log within the intake's
// limit, the file the game leaves for the launcher, and what each answer of
// the intake makes of that file.

#include <doctest/doctest.h>

#include "ember/DesyncReport.hxx"
#include "ember/DesyncTrace.hxx"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace sfve::adapter::desync;

namespace {

const std::string kBuild = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

Facts SampleFacts()
{
	Facts facts;
	facts.slot = 1;
	facts.roomEpoch = 0x1122334455667788ull;
	facts.table = 2;
	facts.generation = 41;
	facts.gamesEnded = 1;
	facts.picksKnown = true;
	facts.fighters[0] = {"RYU", 1, 3, 0, 1};
	facts.fighters[1] = {"CHUN", 2, 5, 1, 0};
	facts.stage = "S01";
	facts.frame = 2580;
	facts.local = 0xabcull;
	facts.remote = 0xdefull;
	facts.confirmed = 2590;
	facts.delay = 2;
	facts.rollbacks = 37;
	facts.longestRollback = 6;
	facts.layoutId = 0x55ull;
	facts.matchBuild = "13173604/00ff00ff00ff00ff/0000000000000055";
	facts.regions = {{"CharaUnit[0].CharaStatusPart+0x238", 64, 0xabull}, {"unowned", 8, 0x1ull}};
	return facts;
}

std::size_t Characters(const std::string& text)
{
	std::size_t count = 0;
	for (const char c : text) count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
	return count;
}

sf4e::HttpResponse Answer(int status, bool ok, const std::string& body = {})
{
	sf4e::HttpResponse answer;
	answer.request.statusCode = status;
	answer.request.ok = ok;
	answer.body = body;
	return answer;
}

} // namespace

TEST_CASE("a desync's title is fixed per build, per kind of copy and per match, so Bugsink groups by it")
{
	CHECK(Title(false, kBuild) == "SFV desync: build 0123456789ab");
	CHECK(Title(true, kBuild) == "SFV desync seen by a watcher: build 0123456789ab");
	CHECK(Title(false, kBuild, "1122334455667788.2.41") == "SFV desync: build 0123456789ab match 1122334455667788.2.41");
	// One match's desync, at any checkpoint, on either fighter's copy, is one issue.
	Facts other = SampleFacts();
	other.frame = 60;
	other.slot = 0;
	other.local = 0xdefull; // the other copy's view of the same mismatch
	other.remote = 0xabcull;
	const std::string a = Comment(SampleFacts(), kBuild), b = Comment(other, kBuild);
	CHECK(a.substr(0, a.find('\n')) == "SFV desync: build 0123456789ab match 1122334455667788.2.41");
	CHECK(a.substr(0, a.find('\n')) == b.substr(0, b.find('\n')));
	// Another match, or another room, is another issue.
	other.generation = 42;
	const std::string c = Comment(other, kBuild);
	CHECK(a.substr(0, a.find('\n')) != c.substr(0, c.find('\n')));
	other = SampleFacts();
	other.roomEpoch = 7;
	CHECK(Comment(other, kBuild).rfind("SFV desync: build 0123456789ab match 0000000000000007.2.41\n", 0) == 0);
	// A watcher's issue is its own, with the same key.
	other = SampleFacts();
	other.watcher = true;
	CHECK(Comment(other, kBuild).rfind("SFV desync seen by a watcher: build 0123456789ab match 1122334455667788.2.41\n", 0) == 0);
	// With no room the title is the build's alone (the pair key in the body still pairs).
	Facts none;
	none.frame = 120;
	CHECK(Comment(none, kBuild).rfind("SFV desync: build 0123456789ab\n", 0) == 0);
}

TEST_CASE("both fighters' copies of one mismatch name it by the same keys")
{
	Facts p1 = SampleFacts(), p2 = SampleFacts();
	p1.slot = 0;
	p2.slot = 1;
	std::swap(p2.local, p2.remote); // each copy's local hash is the other's remote
	CHECK(MatchKey(p1) == "1122334455667788.2.41");
	CHECK(MatchKey(p1) == MatchKey(p2));
	CHECK(PairKey(p1) == "2580:0000000000000abc-0000000000000def");
	CHECK(PairKey(p1) == PairKey(p2));
	CHECK(MatchKey(Facts{}).empty());
	// The comment says both, for a script that pairs reports.
	const std::string comment = Comment(p1, kBuild);
	CHECK(comment.find("Pair key: match 1122334455667788.2.41, mismatch 2580:0000000000000abc-0000000000000def\n") !=
		std::string::npos);
	CHECK(Comment(Facts{}, kBuild).find("Pair key: match not known, mismatch 0:0000000000000000-0000000000000000\n") !=
		std::string::npos);
}

TEST_CASE("a desync's comment names the match, this copy, both picks and the checkpoint")
{
	const std::string comment = Comment(SampleFacts(), kBuild);
	CHECK(comment.rfind("SFV desync: build 0123456789ab\n", 0) == 0);
	CHECK(comment.find("Room 1122334455667788, table 2, match 41; games ended in this battle: 1\n") != std::string::npos);
	CHECK(comment.find("This copy: P2 (match slot 1)\n") != std::string::npos);
	CHECK(comment.find("P1: RYU, costume 1, colour 3, V-Skill I, V-Trigger II\n") != std::string::npos);
	CHECK(comment.find("P2: CHUN, costume 2, colour 5, V-Skill II, V-Trigger I\n") != std::string::npos);
	CHECK(comment.find("Stage: S01\n") != std::string::npos);
	CHECK(comment.find("Checkpoint 2580: 0000000000000abc here, 0000000000000def at the other player; confirmed to 2590\n") !=
		std::string::npos);
	CHECK(comment.find("Input delay 2; 37 rollbacks, the longest 6 frames\n") != std::string::npos);
	CHECK(comment.find("Layout 0000000000000055, 2 regions at the checkpoint; match build 13173604/") != std::string::npos);
	CHECK(Characters(comment) <= kCommentCharacters);
}

TEST_CASE("a start state's repair is reported under a title of its own, with both hashes and what was taken")
{
	CHECK(StartRepairTitle(false, kBuild) == "SFV start state repaired: build 0123456789ab");
	CHECK(StartRepairTitle(true, kBuild) == "SFV start state repaired for a watcher: build 0123456789ab");
	Facts facts = SampleFacts();
	facts.frame = 0;
	facts.confirmed = -1;
	facts.startRepair = "1 region(s) differed from P1's: 2 dword(s) taken from P1.";
	const std::string comment = Comment(facts, kBuild);
	CHECK(comment.rfind("SFV start state repaired: build 0123456789ab\n", 0) == 0);
	CHECK(comment.find("Start state before frame 0: 0000000000000abc here, 0000000000000def at P1. 1 region(s) differed") !=
		std::string::npos);
	CHECK(comment.find("Checkpoint ") == std::string::npos);
	CHECK(RegionLines(facts).find("desync: checkpoint 0 region CharaUnit[0].CharaStatusPart+0x238, 64 bytes") == 0);
	facts.watcher = true;
	const std::string watched = Comment(facts, kBuild);
	CHECK(watched.rfind("SFV start state repaired for a watcher: build", 0) == 0);
	CHECK(watched.find("diagnostic only") == std::string::npos);
}

TEST_CASE("a watcher's comment says its mismatch is a diagnostic; what is not known says so")
{
	Facts facts;
	facts.watcher = true;
	facts.slot = 3;
	facts.frame = 120;
	const std::string comment = Comment(facts, kBuild);
	CHECK(comment.rfind("SFV desync seen by a watcher: build 0123456789ab\n", 0) == 0);
	CHECK(comment.find("This copy: a watcher (match slot 3); its mismatch is a diagnostic only") != std::string::npos);
	CHECK(comment.find("at a fighter") != std::string::npos);
	CHECK(comment.find("Room not known") != std::string::npos);
	CHECK(comment.find("Picks not known") != std::string::npos);
	CHECK(comment.find("Stage: not known") != std::string::npos);
}

TEST_CASE("a desync's comment stays within the intake's 2000 characters")
{
	Facts facts = SampleFacts();
	facts.matchBuild = std::string(3000, 'x');
	CHECK(Characters(Comment(facts, kBuild)) == kCommentCharacters);
	// Cut by characters, never inside one.
	facts.matchBuild.clear();
	// The comment's other lines are about 470 characters: the limit falls
	// inside the é's.
	for (int i = 0; i < 1600; ++i) facts.matchBuild += "\xC3\xA9"; // é
	const std::string comment = Comment(facts, kBuild);
	CHECK(Characters(comment) == kCommentCharacters);
	CHECK((static_cast<unsigned char>(comment.back()) & 0xC0) == 0x80); // ends on a whole é
	CHECK(static_cast<unsigned char>(comment[comment.size() - 2]) == 0xC3);
}

TEST_CASE("a desync's region lines are LogCheckpointRegions' lines")
{
	CHECK(RegionLines(SampleFacts()) ==
		"desync: checkpoint 2580 region CharaUnit[0].CharaStatusPart+0x238, 64 bytes, hash 00000000000000ab\n"
		"desync: checkpoint 2580 region unowned, 8 bytes, hash 0000000000000001\n");
	CHECK(RegionLines(Facts{}).empty());
}

TEST_CASE("the log's tail is the lines before the desync, without that checkpoint's region lines")
{
	const std::string log =
		"partial line\r\n"
		"a\r\n"
		"desync: checkpoint 60 region X, 8 bytes, hash 0000000000000001\n"
		"b\n"
		"desync: checkpoint 2580 is 0000000000000abc here and 0000000000000def at the other player; the fight ends\n"
		"desync: checkpoint 2580 region X, 8 bytes, hash 0000000000000001\n"
		"desync: checkpoint 2580 region Y, 8 bytes, hash 0000000000000002\n"
		"c";
	CHECK(TailLines(log, false, 2580) ==
		"partial line\na\ndesync: checkpoint 60 region X, 8 bytes, hash 0000000000000001\nb\n"
		"desync: checkpoint 2580 is 0000000000000abc here and 0000000000000def at the other player; the fight ends\nc\n");
	// Read from inside the log, its first line is partial and goes.
	CHECK(TailLines(log, true, 2580).rfind("a\n", 0) == 0);
	// The newest lines are kept.
	CHECK(TailLines(log, false, 2580, 2) ==
		"desync: checkpoint 2580 is 0000000000000abc here and 0000000000000def at the other player; the fight ends\nc\n");
	CHECK(TailLines("", false, 2580).empty());
	CHECK(TailLines("one line", true, 2580).empty());
}

TEST_CASE("the report's log holds the comment, the regions and as much of the tail as fits")
{
	const std::string comment = Comment(SampleFacts(), kBuild), regions = RegionLines(SampleFacts());
	const std::string record = Record(comment, regions, "x\ny\n");
	CHECK(record == comment + "\n\n" + regions + "\nThe last lines of sfv_ember.log before the desync (redacted):\nx\ny\n");
	// A tail too long loses its oldest lines, whole.
	std::string tail;
	for (int i = 0; i < 40000; ++i) tail += "line " + std::to_string(i) + "\n";
	const std::string bounded = Record(comment, regions, tail);
	CHECK(bounded.size() <= kLogBytes);
	CHECK(bounded.size() > kLogBytes - 64);
	CHECK(bounded.find(regions) != std::string::npos);
	CHECK(bounded.substr(bounded.size() - 11) == "line 39999\n");
	const std::size_t first = bounded.find("(redacted):\n") + 12;
	CHECK(bounded.compare(first, 5, "line ") == 0);
	// Region lines past the limit go, never the comment, and whole lines only.
	std::string many;
	for (int i = 0; i < 4000; ++i) many += "desync: checkpoint 1 region R" + std::to_string(i) + ", 8 bytes, hash 0000000000000001\n";
	const std::string cut = Record(comment, many, tail);
	CHECK(cut.size() <= kLogBytes);
	CHECK(cut.rfind(comment, 0) == 0);
	CHECK(cut.back() == '\n');
}

TEST_CASE("the game's file for the launcher reads back as written")
{
	Spooled spooled;
	spooled.time = 1794100000;
	spooled.appVersion = "0.1.0-beta.3";
	spooled.buildId = kBuild;
	spooled.sourceRevision = "d57ebfa01234-dirty";
	spooled.watcher = true;
	spooled.comment = Comment(SampleFacts(), kBuild);
	spooled.regionLines = RegionLines(SampleFacts());
	spooled.logTail = "a\nb\n";
	const std::optional<Spooled> back = ParseSpool(SpoolJson(spooled));
	REQUIRE(back);
	CHECK(back->time == spooled.time);
	CHECK(back->appVersion == spooled.appVersion);
	CHECK(back->buildId == spooled.buildId);
	CHECK(back->sourceRevision == spooled.sourceRevision);
	CHECK(back->watcher);
	CHECK(back->comment == spooled.comment);
	CHECK(back->regionLines == spooled.regionLines);
	CHECK(back->logTail == spooled.logTail);

	// A log line that is not UTF-8 is kept, its bad byte as U+FFFD.
	spooled.logTail = "bad \xFF byte\n";
	const std::optional<Spooled> replaced = ParseSpool(SpoolJson(spooled));
	REQUIRE(replaced);
	CHECK(replaced->logTail == "bad \xEF\xBF\xBD byte\n");
}

TEST_CASE("a file that is not the game's report is not read as one")
{
	Spooled spooled;
	spooled.time = 1;
	spooled.appVersion = "0.1.0";
	spooled.buildId = kBuild;
	spooled.sourceRevision = "r";
	spooled.comment = "SFV desync: build 0123456789ab";
	REQUIRE(ParseSpool(SpoolJson(spooled)));
	CHECK_FALSE(ParseSpool(""));
	CHECK_FALSE(ParseSpool("{"));
	CHECK_FALSE(ParseSpool("[]"));
	Spooled bad = spooled;
	bad.buildId = kBuild.substr(1);
	CHECK_FALSE(ParseSpool(SpoolJson(bad)));
	bad.buildId = std::string(64, 'g');
	CHECK_FALSE(ParseSpool(SpoolJson(bad)));
	bad = spooled;
	bad.comment.clear();
	CHECK_FALSE(ParseSpool(SpoolJson(bad)));
	bad.comment = std::string(kCommentCharacters + 1, 'x');
	CHECK_FALSE(ParseSpool(SpoolJson(bad)));
	std::string json = SpoolJson(spooled);
	const std::size_t schema = json.find("\"schema\": 1");
	REQUIRE(schema != std::string::npos);
	CHECK_FALSE(ParseSpool(json.replace(schema, 11, "\"schema\": 2")));
	json = SpoolJson(spooled);
	const std::size_t watcher = json.find("\"watcher\": false");
	REQUIRE(watcher != std::string::npos);
	CHECK_FALSE(ParseSpool(json.replace(watcher, 16, "\"watcher\": 0")));
}

TEST_CASE("each answer of the intake settles the report's file or keeps it")
{
	const Outcome received = Judge(Answer(202, true, R"({"id":"0123456789abcdef0123456789abcdef"})"), true);
	CHECK(received.verdict == Verdict::Delivered);
	CHECK(received.id == "0123456789abcdef0123456789abcdef");
	CHECK(received.reason.empty());
	const Outcome noId = Judge(Answer(202, true, R"({"id":"../x"})"), true);
	CHECK(noId.verdict == Verdict::Delivered);
	CHECK(noId.id.empty());
	CHECK(noId.reason == "no_id");
	const Outcome incomplete = Judge(Answer(202, false), true);
	CHECK(incomplete.verdict == Verdict::Delivered);
	CHECK(incomplete.reason == "incomplete");

	// An intake from before SFV Ember's logs: once more without the log.
	const std::string unknownLog = R"({"reason":"unknown_log_filename"})";
	CHECK(Judge(Answer(400, false, unknownLog), true).verdict == Verdict::WithoutLog);
	const Outcome refusedBare = Judge(Answer(400, false, unknownLog), false);
	CHECK(refusedBare.verdict == Verdict::Refused);
	CHECK(refusedBare.reason == "unknown_log_filename");
	const Outcome invalid = Judge(Answer(400, false, R"({"reason":"invalid_metadata"})"), true);
	CHECK(invalid.verdict == Verdict::Refused);
	CHECK(invalid.reason == "invalid_metadata");
	const Outcome odd = Judge(Answer(413, false, R"({"reason":"Not A Code"})"), true);
	CHECK(odd.verdict == Verdict::Refused);
	CHECK(odd.reason == "http_413");

	const Outcome limited = Judge(Answer(429, false, R"({"reason":"rate_limit"})"), true);
	CHECK(limited.verdict == Verdict::Later);
	CHECK(limited.reason == "rate_limit");
	CHECK(Judge(Answer(503, false), true).verdict == Verdict::Later);
	CHECK(Judge(Answer(408, false), true).verdict == Verdict::Later);

	sf4e::HttpResponse timedOut = Answer(0, false);
	timedOut.request.error = sf4e::HttpErrorKind::Timeout;
	CHECK(Judge(timedOut, true).verdict == Verdict::Later);
	CHECK(Judge(timedOut, true).reason == "timeout");
	sf4e::HttpResponse offline = Answer(0, false);
	offline.request.error = sf4e::HttpErrorKind::ConnectFailed;
	CHECK(Judge(offline, true).reason == "no_connection");
	sf4e::HttpResponse cancelled = Answer(0, false);
	cancelled.cancelled = true;
	CHECK(Judge(cancelled, true).verdict == Verdict::Later);
	CHECK(Judge(cancelled, true).reason == "cancelled");
}

TEST_CASE("past the limit the oldest waiting reports go, never one the launcher holds")
{
	namespace fs = std::filesystem;
	const fs::path folder = fs::temp_directory_path() / ("sfve-spool-test-" + std::to_string(GetCurrentProcessId()));
	fs::remove_all(folder);
	CHECK(MakeRoom(folder, 4).waiting == 0); // no folder: nothing waits
	fs::create_directories(folder);
	const auto write = [&](const std::string& name) { std::ofstream(folder / name, std::ios::binary) << "x"; };
	for (const char* name : {"1000-1-10.desync.json", "1001-1-20.desync.json", "1002-1-30.desync.json"}) write(name);
	write("1003-1-40.tmp"); // a report being written is not waiting
	write("notes.txt"); // nor is a file that is not a report

	// Room for one more already: nothing goes.
	Room room = MakeRoom(folder, 4);
	CHECK(room.waiting == 3);
	CHECK(room.removed.empty());

	// At the limit the oldest goes, so the new report is the one kept.
	write("1004-1-50.desync.json");
	room = MakeRoom(folder, 4);
	REQUIRE(room.removed.size() == 1);
	CHECK(room.removed[0] == L"1000-1-10.desync.json");
	CHECK(room.waiting == 3);

	// The launcher holds the oldest (open, unshared, as DesyncCourier does):
	// it stays and the next oldest goes.
	write("1005-1-60.desync.json");
	const HANDLE held = CreateFileW((folder / "1001-1-20.desync.json").c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(held != INVALID_HANDLE_VALUE);
	room = MakeRoom(folder, 4);
	REQUIRE(room.removed.size() == 1);
	CHECK(room.removed[0] == L"1002-1-30.desync.json");
	CHECK(room.waiting == 3);
	// Every report held: nothing can go and the count stays at the limit.
	const HANDLE held2 = CreateFileW((folder / "1004-1-50.desync.json").c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	const HANDLE held3 = CreateFileW((folder / "1005-1-60.desync.json").c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(held2 != INVALID_HANDLE_VALUE);
	REQUIRE(held3 != INVALID_HANDLE_VALUE);
	room = MakeRoom(folder, 3);
	CHECK(room.waiting == 3);
	CHECK(room.removed.empty());
	CloseHandle(held);
	CloseHandle(held2);
	CloseHandle(held3);

	// What is left is the newest, and the other files are untouched.
	CHECK(fs::exists(folder / "1003-1-40.tmp"));
	CHECK(fs::exists(folder / "notes.txt"));
	fs::remove_all(folder);
}

namespace {

void PutLittle(std::vector<std::uint8_t>& out, std::size_t at, std::uint64_t value, int bytes)
{
	for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
}

// A file's little-endian field.
std::uint64_t FileValue(const std::string& file, std::size_t at, int bytes)
{
	std::uint64_t v = 0;
	for (int i = 0; i < bytes; ++i) v |= std::uint64_t(static_cast<unsigned char>(file[at + i])) << (8 * i);
	return v;
}

bool LooksLike(std::uint64_t value)
{
	return value >= (1ull << 40) && value <= 0x00007FFFFFFFFFFFull && (value & 3) == 0; // mod/GameMemory.hxx
}

// Two fighters' action parts, ActionUnit runs and timeline objects, as the
// game lays them out (tools/stateregion.py --fighters): P1 plays script 1200
// at frame 200.5 mirrored, P2 script 212 at frame 0.
Facts FighterState()
{
	Facts facts;
	std::vector<std::uint8_t> state;
	const auto region = [&](const std::string& name, std::uint64_t size, std::uint64_t address) {
		facts.stateRegions.push_back({name, size, address});
		state.resize(state.size() + static_cast<std::size_t>(size), 0);
		return state.size() - static_cast<std::size_t>(size);
	};
	struct Fighter {
		int side;
		std::uint64_t part, timeline;
		std::int32_t script;
		std::int32_t frame16;
		std::int32_t facing;
	};
	const Fighter fighters[2] = {{0, 0x20000010000ull, 0x20000050000ull, 1200, 200 * 65536 + 32768, 1},
		{1, 0x20000020000ull, 0x20000060000ull, 212, 0, 0}};
	for (int i = 0; i < 2; ++i) {
		const std::string unit = "CharaUnit[" + std::to_string(i) + "]";
		const std::size_t part = region(unit + ".CharaActionPart+0xD8", 15808, fighters[i].part);
		PutLittle(state, part, fighters[i].timeline, 8); // the script player now playing
		PutLittle(state, part + 0x3E90 - 0xD8, static_cast<std::uint32_t>(fighters[i].facing), 4);
		const std::size_t slot = region(unit + "+0xF8", 16, 0x20000030000ull + i * 0x1000);
		PutLittle(state, slot + 8, static_cast<std::uint32_t>(fighters[i].side), 4);
		// The timeline object's run starts 0x30 into it: its script number is object +0x48, its frame +0x4C.
		const std::size_t timeline = region("CharaUnit[" + std::to_string(i) + "].CharaActionPart->0x3EC8+0x30", 0x40,
			fighters[i].timeline + 0x30);
		PutLittle(state, timeline + 0x18, static_cast<std::uint32_t>(fighters[i].script), 4);
		PutLittle(state, timeline + 0x1C, static_cast<std::uint32_t>(fighters[i].frame16), 4);
	}
	facts.state = std::move(state);
	return facts;
}

} // namespace

TEST_CASE("each fighter's script and frame are read from the kept state, as the frame meter reads them")
{
	const Facts facts = FighterState();
	const std::vector<ScriptFacts> scripts = ScriptsAt(facts);
	REQUIRE(scripts.size() == 2);
	CHECK(scripts[0].unit == "CharaUnit[0]");
	CHECK(scripts[0].side == 0);
	CHECK(scripts[0].script == 1200);
	CHECK(scripts[0].frame == doctest::Approx(200.5));
	CHECK(scripts[0].facing == 1);
	CHECK(scripts[1].side == 1);
	CHECK(scripts[1].script == 212);
	CHECK(scripts[1].frame == doctest::Approx(0.0));
	CHECK(scripts[1].facing == 0);

	// In the comment and the details, by side.
	Facts reported = facts;
	reported.frame = 2580;
	const std::string comment = Comment(reported, kBuild);
	CHECK(comment.find("Scripts at the checkpoint: P1 script 1200 frame 200.50, mirrored; P2 script 212 frame 0.00\n") !=
		std::string::npos);

	// A fighter whose script player is not in the state has no script; a state the
	// regions do not account for, or none, says nothing.
	Facts lost = facts;
	PutLittle(lost.state, 0, 0x2ull << 40, 8);
	const std::vector<ScriptFacts> unknown = ScriptsAt(lost);
	REQUIRE(unknown.size() == 2);
	CHECK(unknown[0].script == -1);
	CHECK(unknown[1].script == 212);
	CHECK(Comment(lost, kBuild).find("P1 no script found; P2 script 212") != std::string::npos);
	lost.state.pop_back();
	CHECK(ScriptsAt(lost).empty());
	CHECK(ScriptsAt(Facts{}).empty());
	CHECK(Comment(Facts{}, kBuild).find("Scripts at the checkpoint") == std::string::npos);
}

TEST_CASE("the comment and the details carry what names a cause from one report")
{
	Facts facts = SampleFacts();
	facts.previousMatch = 2560;
	facts.now = 2591;
	facts.recent = {true, 2471, 2591, 5, 4};
	facts.socketReads = "12 fresh read(s) recorded, 3 conflicting";
	facts.inputs.known = true;
	facts.inputs.first = 2400;
	facts.inputs.end = 2580;
	facts.inputs.runs = {{2400, 100, 0x10, 0x0, 2}, {2500, 80, 0x1A, 0x4000, 2}};
	const std::string comment = Comment(facts, kBuild);
	CHECK(comment.find("Previous matching checkpoint: 2560\n") != std::string::npos);
	CHECK(comment.find("Input delay 2; 37 rollbacks, the longest 6 frames\n") != std::string::npos);
	CHECK(comment.find("Last 120 frames (2471 to 2591): 5 rollbacks, the longest 4 frames\n") != std::string::npos);
	CHECK(comment.find("Socket reads: 12 fresh read(s) recorded, 3 conflicting\n") != std::string::npos);
	CHECK(comment.find("Inputs of frames 2400 to 2580 are in sfv_ember.log\n") != std::string::npos);
	CHECK(Characters(comment) <= kCommentCharacters);

	const std::string details = DetailLines(facts);
	CHECK(details.find("pair key: match 1122334455667788.2.41, mismatch 2580:0000000000000abc-0000000000000def\n") != std::string::npos);
	CHECK(details.find("this copy: P2 (match slot 1), input delay 2\n") != std::string::npos);
	CHECK(details.find("P1: RYU, costume 1, colour 3, V-Skill I, V-Trigger II\n") != std::string::npos);
	CHECK(details.find("checkpoint 2580, found at session frame 2591, confirmed to 2590; previous matching checkpoint 2560\n") !=
		std::string::npos);
	CHECK(details.find("rollbacks, whole battle: 37, the longest 6 frames\n") != std::string::npos);
	CHECK(details.find("rollbacks, frames 2471 to 2591: 5, the longest 4 frames\n") != std::string::npos);
	CHECK(details.find("socket reads: 12 fresh read(s) recorded, 3 conflicting\n") != std::string::npos);
	CHECK(details.find("input 2400 +100 P1 00000010 P2 00000000 f2\n") != std::string::npos);
	CHECK(details.find("input 2500 +80 P1 0000001a P2 00004000 f2\n") != std::string::npos);

	// What is not known says so, and a first checkpoint that already differs has no previous one.
	const std::string bare = Comment(Facts{}, kBuild);
	CHECK(bare.find("Previous matching checkpoint: none (the first checkpoint already differs)\n") != std::string::npos);
	CHECK(bare.find("Inputs: not recorded\n") != std::string::npos);
	CHECK(bare.find("Last 120 frames") == std::string::npos);
	CHECK(bare.find("Socket reads") == std::string::npos);
	const std::string bareDetails = DetailLines(Facts{});
	CHECK(bareDetails.find("inputs: not recorded") != std::string::npos);
	CHECK(bareDetails.find("rollbacks, last 120 frames: not counted\n") != std::string::npos);
	CHECK(bareDetails.find("socket reads: not read\n") != std::string::npos);
	CHECK(bareDetails.find("scripts at the checkpoint: not read (the state was not kept)\n") != std::string::npos);

	// The details come before the region lines in the report's log, ahead of the tail.
	const std::string record = Record(comment, RegionLines(facts), "x\n", details);
	CHECK(record == comment + "\n\n" + details + "\n" + RegionLines(facts) +
		"\nThe last lines of sfv_ember.log before the desync (redacted):\nx\n");
}

TEST_CASE("a worst-case report's comment and log stay within the intake's limits")
{
	Facts facts = SampleFacts();
	facts.recent = {true, 1, 121, 99999, 99999};
	facts.socketReads = std::string(520, 's'); // SocketReadsSummary's text with every count at 20 digits
	facts.inputs.known = true;
	facts.inputs.first = 0;
	facts.inputs.end = kInputFrames;
	// Mashing: every frame its own run.
	for (int f = 0; f < kInputFrames; ++f) facts.inputs.runs.push_back({f, 1, 0xFFFFFFFFu, 0xFFFFFFFFu, 3});
	CHECK(Characters(Comment(facts, kBuild)) <= kCommentCharacters);
	const std::string details = DetailLines(facts);
	CHECK(details.size() < 16 * 1024); // 180 runs, about 40 bytes each
	// 400 regions, as SFV's layout has, still leave most of the log for the tail.
	std::string regions;
	for (int i = 0; i < 400; ++i) regions += "desync: checkpoint 2580 region CharaUnit[0].CharaStatusPart+0x238, 64 bytes, hash 00000000000000ab\n";
	CHECK(Record(Comment(facts, kBuild), regions, "tail\n", details).size() < kLogBytes / 2);
}

TEST_CASE("a state file of version 2 holds the regions with their flags and an empty census when none was taken")
{
	Facts facts;
	facts.stateRegions = {{"CharaUnit[0].CharaActionPart+0xD8", 3, 0x1000, true, true}, {"global.rng", 2, 0x7FF600000000ull, true, false}};
	facts.state = {1, 2, 3, 4, 5};
	const std::string file = StateFile(facts);
	REQUIRE(file.size() == 137);
	CHECK(file.substr(0, 8) == "SFVSTATE");
	CHECK(FileValue(file, 8, 4) == 2);
	CHECK(FileValue(file, 12, 4) == 2);
	CHECK(FileValue(file, 16, 2) == 33);
	CHECK(file.substr(18, 33) == "CharaUnit[0].CharaActionPart+0xD8");
	CHECK(FileValue(file, 51, 8) == 3);
	CHECK(FileValue(file, 59, 8) == 0x1000);
	CHECK(FileValue(file, 67, 4) == 3); // checked, in the peers' hash
	CHECK(file.substr(71, 3) == std::string("\x01\x02\x03", 3));
	CHECK(FileValue(file, 74, 2) == 10);
	CHECK(file.substr(76, 10) == "global.rng");
	CHECK(FileValue(file, 94, 8) == 0x7FF600000000ull);
	CHECK(FileValue(file, 102, 4) == 1); // checked, not in the peers' hash
	CHECK(file.substr(106, 2) == std::string("\x04\x05", 2));
	CHECK(FileValue(file, 108, 4) == 0);  // objects
	CHECK(FileValue(file, 112, 4) == 0);  // modules
	CHECK(FileValue(file, 133, 4) == 0);  // words

	facts.state.pop_back();
	CHECK(StateFile(facts).empty());
	facts.stateRegions.clear();
	facts.state.clear();
	CHECK(StateFile(facts).empty());
}

namespace {

// A process whose memory is a few fixed places.
class FakeProcess final : public PointerEnv {
public:
	PointerTarget Look(std::uint64_t address) override
	{
		PointerTarget t;
		if (address == 0x40000000000ull) return t; // unmapped
		t.mapped = true;
		t.hash = 0x1000 + (address & 0xFF);
		if (address >= 0x7FFA00000000ull && address < 0x7FFA00100000ull) {
			t.allocBase = t.moduleBase = 0x7FFA00000000ull;
			t.allocSize = 0x100000;
			t.module = "foo.dll";
		} else if (address >= 0x7FF600000000ull && address < 0x7FF600100000ull) {
			t.allocBase = t.moduleBase = 0x7FF600000000ull;
			t.allocSize = 0x100000;
			t.module = "StreetFighterV.exe";
		} else {
			t.allocBase = address & ~0xFFFull;
			t.allocSize = 0x1000;
		}
		return t;
	}
};

} // namespace

TEST_CASE("the pointer census classes every address-like word of a peer-hashed region by what it points at")
{
	// Region A: 8 words from 0x20000000000. Region B: a target. Region C is not in the peers'
	// hash. Region D lies 4 bytes past an 8-byte boundary, so its words start at +4.
	Facts facts;
	std::vector<std::uint8_t> state;
	const auto add = [&](const std::string& name, std::uint64_t size, std::uint64_t address, bool peers) {
		facts.stateRegions.push_back({name, size, address, true, peers});
		state.resize(state.size() + static_cast<std::size_t>(size), 0);
		return state.size() - static_cast<std::size_t>(size);
	};
	const std::size_t a = add("A", 64, 0x20000000000ull, true);
	add("B", 32, 0x20000001000ull, true);
	const std::size_t c = add("C", 16, 0x20000002000ull, false);
	const std::size_t d = add("D", 20, 0x20000003004ull, true);
	PutLittle(state, a + 0, 0x7FF600001234ull, 8);   // the exe
	PutLittle(state, a + 8, 0x20000001008ull, 8);    // region B, +8
	PutLittle(state, a + 16, 0x30000000010ull, 8);   // a named object, +0x10
	PutLittle(state, a + 24, 0x38000000020ull, 8);   // heap
	PutLittle(state, a + 32, 0x40000000000ull, 8);   // not mapped
	PutLittle(state, a + 40, 0x7FFA00001000ull, 8);  // another module
	PutLittle(state, a + 48, 0x1234, 8);             // a plain value: no census word
	PutLittle(state, a + 56, 0x20000001002ull, 8);   // an address-like word with bit 1 set: not an address
	PutLittle(state, c + 0, 0x7FF600001234ull, 8);   // not in the peers' hash: left out
	PutLittle(state, d + 4, 0x38000000020ull, 8);    // D's first word is at +4
	facts.state = state;

	PointerCensus seed;
	seed.imageBase = 0x7FF600000000ull;
	seed.imageSize = 0x100000;
	seed.objects = {{"CharaUnit[0]", 0x30000000000ull, 0x800}};
	FakeProcess process;
	const PointerCensus census = TakeCensus(facts, process, &LooksLike, seed);
	REQUIRE(census.words.size() == 7);
	CHECK_FALSE(census.truncated);
	const auto& w = census.words;
	CHECK(w[0].region == 0);
	CHECK(w[0].offset == 0);
	CHECK(w[0].cls == PointerClass::Image);
	CHECK(w[0].reference == 0x1234);
	CHECK(w[1].cls == PointerClass::Region);
	CHECK(w[1].reference == 8);
	CHECK(w[1].refIndex == 1);
	CHECK(w[2].cls == PointerClass::Object);
	CHECK(w[2].reference == 0x10);
	CHECK(w[2].refIndex == 0);
	CHECK(w[3].cls == PointerClass::Heap);
	CHECK(w[3].reference == 0x20);
	CHECK(w[3].allocBase == 0x38000000000ull);
	CHECK(w[3].allocSize == 0x1000);
	CHECK(w[3].targetHash == 0x1020);
	CHECK(w[4].cls == PointerClass::Unmapped);
	CHECK_FALSE(w[4].mapped);
	CHECK(w[4].targetHash == 0);
	CHECK(w[5].cls == PointerClass::Module);
	CHECK(w[5].reference == 0x1000);
	REQUIRE(census.modules.size() == 1);
	CHECK(census.modules[0] == "foo.dll");
	CHECK(w[6].region == 3);
	CHECK(w[6].offset == 4);
	CHECK(w[6].cls == PointerClass::Heap);

	// Written after the regions: the object list, the modules, the image and the words.
	const std::string file = StateFile(facts, &census);
	REQUIRE_FALSE(file.empty());
	std::size_t at = 16;
	for (int r = 0; r < 4; ++r) at += 2 + FileValue(file, at, 2) + 8 + 8 + 4 + static_cast<std::size_t>(facts.stateRegions[r].size);
	CHECK(FileValue(file, at, 4) == 1); // one object
	at += 4;
	CHECK(FileValue(file, at, 2) == 12);
	CHECK(file.substr(at + 2, 12) == "CharaUnit[0]");
	CHECK(FileValue(file, at + 14, 8) == 0x30000000000ull);
	CHECK(FileValue(file, at + 22, 8) == 0x800);
	at += 30;
	CHECK(FileValue(file, at, 4) == 1); // one module
	CHECK(FileValue(file, at + 4, 2) == 7);
	CHECK(file.substr(at + 6, 7) == "foo.dll");
	at += 13;
	CHECK(FileValue(file, at, 8) == 0x7FF600000000ull);
	CHECK(FileValue(file, at + 8, 8) == 0x100000);
	CHECK(FileValue(file, at + 16, 1) == 0); // not truncated
	CHECK(FileValue(file, at + 17, 4) == 7);
	at += 21;
	REQUIRE(file.size() == at + 7 * 60);
	CHECK(FileValue(file, at + 8, 8) == 0x7FF600001234ull);   // word 0's value
	CHECK(FileValue(file, at + 16, 1) == static_cast<unsigned>(PointerClass::Image));
	CHECK(FileValue(file, at + 17, 1) == 1);                  // mapped
	CHECK(FileValue(file, at + 20, 8) == 0x1234);             // its reference
	CHECK(FileValue(file, at + 60 + 16, 1) == static_cast<unsigned>(PointerClass::Region));
	CHECK(FileValue(file, at + 60 + 28, 4) == 1);             // the region's number (B)
	CHECK(FileValue(file, at + 3 * 60 + 36, 8) == 0x38000000000ull); // word 3's allocation base
	CHECK(FileValue(file, at + 3 * 60 + 52, 8) == 0x1020);    // and the hash of its target

	// The words past the cap are not listed, and the file says so.
	Facts many;
	many.stateRegions = {{"M", 8 * (kCensusWords + 10), 0x20000000000ull, true, true}};
	many.state.assign(8 * (kCensusWords + 10), 0);
	for (std::size_t i = 0; i < kCensusWords + 10; ++i) PutLittle(many.state, i * 8, 0x7FF600000010ull, 8);
	const PointerCensus capped = TakeCensus(many, process, &LooksLike, seed);
	CHECK(capped.words.size() == kCensusWords);
	CHECK(capped.truncated);

	// A state the regions do not account for has no census.
	Facts broken = facts;
	broken.state.pop_back();
	CHECK(TakeCensus(broken, process, &LooksLike, seed).words.empty());
}

TEST_CASE("the trace keeps both players' inputs in runs and the rollbacks by frame")
{
	DesyncTrace trace;
	const auto frame = [](std::uint32_t p1, std::uint32_t p2, std::uint8_t flags = sfve::RecordedFrame::kFight) {
		sfve::RecordedFrame f;
		f.inputs.p[0] = p1;
		f.inputs.p[1] = p2;
		f.flags = flags;
		return f;
	};
	CHECK_FALSE(trace.Inputs(0, 100).known); // nothing noted: not recorded
	for (int f = 0; f < 300; ++f) trace.NoteFrame(f, frame(f < 150 ? 0x10 : 0x20, 0x01));
	// A rollback plays frames 290 to 299 again with the inputs confirmed meanwhile: the last report stands.
	for (int f = 290; f < 300; ++f) trace.NoteFrame(f, frame(0x30, 0x01));
	desync::InputFacts in = trace.Inputs(100, 300);
	REQUIRE(in.known);
	CHECK(in.first == 100);
	CHECK(in.end == 300);
	REQUIRE(in.runs.size() == 3);
	CHECK(in.runs[0].first == 100);
	CHECK(in.runs[0].frames == 50);
	CHECK(in.runs[0].p1 == 0x10);
	CHECK(in.runs[1].first == 150);
	CHECK(in.runs[1].frames == 140);
	CHECK(in.runs[1].p1 == 0x20);
	CHECK(in.runs[2].first == 290);
	CHECK(in.runs[2].frames == 10);
	CHECK(in.runs[2].p1 == 0x30);
	CHECK(in.runs[2].p2 == 0x01);
	// A held frame or one that ignores the inputs is a run of its own.
	trace.NoteFrame(300, frame(0x30, 0x01, sfve::RecordedFrame::kHeld));
	in = trace.Inputs(298, 301);
	REQUIRE(in.runs.size() == 2);
	CHECK(in.runs[0].frames == 2);
	CHECK(in.runs[1].flags == sfve::RecordedFrame::kHeld);
	// A window that starts before the battle holds its first frame's run onwards.
	trace = DesyncTrace();
	for (int f = 0; f < 50; ++f) trace.NoteFrame(f, frame(0x1, 0x2));
	in = trace.Inputs(-130, 50);
	CHECK(in.first == 0);
	CHECK(in.end == 50);
	// A window the ring lost (older than its size) is not known.
	for (int f = 50; f < 2000; ++f) trace.NoteFrame(f, frame(0x1, 0x2));
	CHECK_FALSE(trace.Inputs(0, 100).known);
	CHECK(trace.Inputs(1900, 2000).known);
	// A new battle's first frame (far behind the newest) starts the ring over.
	trace.NoteFrame(0, frame(0x7, 0x7));
	in = trace.Inputs(0, 2000);
	REQUIRE(in.known);
	CHECK(in.end == 1);
	CHECK(in.runs[0].p1 == 0x7);
}

TEST_CASE("the trace counts a window's rollbacks and its longest from the session's counters")
{
	DesyncTrace trace;
	// frame, rollbacks so far, frames re-simulated so far
	trace.NoteStep(100, 0, 0);
	trace.NoteStep(101, 1, 3); // a rollback of 3
	trace.NoteStep(150, 1, 3);
	trace.NoteStep(200, 3, 12); // two rollbacks in one step: 9 frames re-simulated
	trace.NoteStep(250, 3, 12);
	trace.NoteStep(260, 4, 16); // a rollback of 4
	desync::RollbackFacts all = trace.Recent(260, 1000);
	CHECK(all.known);
	CHECK(all.count == 4);
	CHECK(all.longest == 9);
	// The last 120 frames: (140, 260].
	desync::RollbackFacts recent = trace.Recent(260, 120);
	CHECK(recent.from == 140);
	CHECK(recent.to == 260);
	CHECK(recent.count == 3);
	CHECK(recent.longest == 9);
	// (200, 260]: the step at 200 is outside.
	recent = trace.Recent(260, 60);
	CHECK(recent.count == 1);
	CHECK(recent.longest == 4);
	// None in the window: counted as none.
	recent = trace.Recent(250, 40);
	CHECK(recent.known);
	CHECK(recent.count == 0);
	CHECK(recent.longest == 0);
	// A session whose counters went back is a new battle's: the ring starts over.
	trace.NoteStep(30, 0, 0);
	trace.NoteStep(40, 1, 2);
	recent = trace.Recent(40, 120);
	CHECK(recent.count == 1);
	CHECK(recent.longest == 2);
	CHECK_FALSE(DesyncTrace().Recent(10, 120).known);
}

TEST_CASE("the courier's caps: a day's, and the intake's pace of ten minutes")
{
	const std::int64_t now = 1800000000;
	std::vector<std::int64_t> sent;
	CHECK(SendWaitSeconds(sent, now) == 0);
	// Four in ten minutes is the pace; the fifth waits for the oldest to leave the window.
	for (int i = 0; i < 4; ++i) sent.push_back(now - 100 * (4 - i)); // 400, 300, 200 and 100 s ago
	CHECK(SendWaitSeconds(sent, now) == 200); // the 400 s old one leaves at 600
	sent[0] = now - 700; // left already
	CHECK(SendWaitSeconds(sent, now) == 0);
	// The day: twelve sends spread over 24 hours (none in the last ten minutes) hold the thirteenth.
	sent.clear();
	for (int i = 0; i < kSendsPerDay; ++i) sent.push_back(now - 3600 * (i + 1));
	CHECK(SendWaitSeconds(sent, now) == 24 * 3600 - 12 * 3600); // the oldest, 12 h ago, leaves in 12 h
	sent.pop_back();
	CHECK(SendWaitSeconds(sent, now) == 0);
	// Older than a day does not count; a send dated after now does.
	sent = std::vector<std::int64_t>(20, now - 24 * 3600);
	CHECK(SendWaitSeconds(sent, now) == 0);
	sent = std::vector<std::int64_t>(4, now + 60);
	CHECK(SendWaitSeconds(sent, now) > 0);
	// Six desyncs in a session (2026-11-13) go out as fast as the intake takes them.
	sent.clear();
	std::int64_t t = now;
	int waited = 0;
	for (int report = 0; report < 6; ++report) {
		while (const std::int64_t wait = SendWaitSeconds(sent, t)) {
			t += wait;
			waited++;
		}
		sent.push_back(t);
	}
	// Four go at once; the fifth and sixth wait together for the ten minutes' window.
	CHECK(waited == 1);
	CHECK(t - now == 600);
}

TEST_CASE("the courier sends the installed build's reports newest first and drops other builds' and stale ones")
{
	const std::int64_t now = 1800000000;
	const std::string installed = kBuild, other(64, 'a');
	const std::vector<Waiting> waiting = {
		{now - 8 * 24 * 3600, installed},  // 0: older than a week
		{now - 3 * 3600, other},           // 1: another build
		{now - 3000, installed},           // 2
		{now - 100, installed},            // 3: newest
		{now - 100, installed},            // 4: written in the same second, later
		{now - 2000, other},               // 5: another build
		{now - 2500, installed},           // 6
	};
	SendPlan plan = PlanSends(waiting, installed, now);
	CHECK(plan.send == std::vector<std::size_t>{4, 3, 6, 2});
	CHECK(plan.otherBuild == std::vector<std::size_t>{1, 5});
	CHECK(plan.stale == std::vector<std::size_t>{0});
	// A stale report of another build is stale, once.
	plan = PlanSends({{now - 9 * 24 * 3600, other}}, installed, now);
	CHECK(plan.stale.size() == 1);
	CHECK(plan.otherBuild.empty());
	// The installed build is not known: no report is dropped for its build.
	plan = PlanSends(waiting, "", now);
	CHECK(plan.send == std::vector<std::size_t>{4, 3, 5, 6, 2, 1});
	CHECK(plan.otherBuild.empty());
	CHECK(PlanSends({}, installed, now).send.empty());
}

TEST_CASE("a part is added to a finished multipart body before its closing boundary")
{
	const std::string body = "--B\r\nContent-Disposition: form-data; name=\"meta\"\r\n\r\n{}\r\n--B--\r\n";
	const std::string with = WithPart(body, "B", "state", "desync.state.z", std::string("\x78\x9c\x00", 3), 4096);
	CHECK(with == "--B\r\nContent-Disposition: form-data; name=\"meta\"\r\n\r\n{}\r\n"
		"--B\r\nContent-Disposition: form-data; name=\"state\"; filename=\"desync.state.z\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n" + std::string("\x78\x9c\x00", 3) + "\r\n--B--\r\n");
	// A body that does not end with the boundary's closing, or that would pass the limit, is left alone.
	CHECK(WithPart("--B\r\nx", "B", "state", "f", "y", 4096).empty());
	CHECK(WithPart(body, "C", "state", "f", "y", 4096).empty());
	CHECK(WithPart(body, "B", "state", "f", "y", body.size()).empty());
	CHECK_FALSE(WithPart(body, "B", "state", "f", "y", 4096).empty());
}

TEST_CASE("an intake that does not take the state part is sent the report once more without it")
{
	CHECK(Judge(Answer(400, false, R"({"reason":"unknown_part"})"), true, true).verdict == Verdict::WithoutState);
	CHECK(Judge(Answer(413, false), true, true).verdict == Verdict::WithoutState);
	// The courier turns the part off only for the first (reason unknown_part); a 413 skips one report's state.
	CHECK(Judge(Answer(400, false, R"({"reason":"unknown_part"})"), true, true).reason == "unknown_part");
	CHECK(Judge(Answer(413, false), true, true).reason != "unknown_part");
	// Without a state in the upload the same answers are the report's own refusals.
	CHECK(Judge(Answer(400, false, R"({"reason":"unknown_part"})"), true, false).verdict == Verdict::Refused);
	CHECK(Judge(Answer(413, false), true, false).verdict == Verdict::Refused);
	// The log's name is answered first when the intake knows neither.
	CHECK(Judge(Answer(400, false, R"({"reason":"unknown_log_filename"})"), true, true).verdict == Verdict::WithoutLog);
	CHECK(Judge(Answer(202, true, R"({"id":"0123456789abcdef0123456789abcdef"})"), true, true).verdict == Verdict::Delivered);
	CHECK(Judge(Answer(429, false), true, true).verdict == Verdict::Later);
}

TEST_CASE("the game's file for the launcher carries the details, and an older file without them still reads")
{
	Spooled spooled;
	spooled.time = 1;
	spooled.appVersion = "0.1.0";
	spooled.buildId = kBuild;
	spooled.sourceRevision = "r";
	spooled.comment = "SFV desync: build 0123456789ab";
	spooled.details = DetailLines(SampleFacts());
	const std::optional<Spooled> back = ParseSpool(SpoolJson(spooled));
	REQUIRE(back);
	CHECK(back->details == spooled.details);
	// A file from before the details had no such key.
	nlohmann::json value = nlohmann::json::parse(SpoolJson(spooled));
	value.erase("details");
	const std::optional<Spooled> older = ParseSpool(value.dump());
	REQUIRE(older);
	CHECK(older->details.empty());
	CHECK(older->comment == spooled.comment);
	// A details field of the wrong kind is not a file this code writes.
	value["details"] = 7;
	CHECK_FALSE(ParseSpool(value.dump()));
}
