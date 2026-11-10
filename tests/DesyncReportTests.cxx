// A desync's report (ember/DesyncReport.hxx): what the comment Bugsink shows
// and groups by holds, the region lines both players' reports list, the
// mod's log tail before the desync, the report's log within the intake's
// limit, the file the game leaves for the launcher, and what each answer of
// the intake makes of that file.

#include <doctest/doctest.h>

#include "ember/DesyncReport.hxx"

#include <string>

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

TEST_CASE("a desync's title is fixed per build and per kind of copy, so Bugsink groups by it")
{
	CHECK(Title(false, kBuild) == "SFV desync: build 0123456789ab");
	CHECK(Title(true, kBuild) == "SFV desync seen by a watcher: build 0123456789ab");
	// Two desyncs of one build at other checkpoints and rooms share it.
	Facts other = SampleFacts();
	other.frame = 60;
	other.roomEpoch = 7;
	const std::string a = Comment(SampleFacts(), kBuild), b = Comment(other, kBuild);
	CHECK(a.substr(0, a.find('\n')) == b.substr(0, b.find('\n')));
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
