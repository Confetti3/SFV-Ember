#pragma once

// A netplay desync's report (EMBER_LIBRARY.md section 51): what the two
// players' reports must hold so the desync can be fixed from them alone, in
// the form the report intake (the library's server/ember-reports) takes. The
// pure parts, shared by the game (which writes the report down as the
// desync is found, DesyncReporter) and the launcher (which sends what the
// game wrote, DesyncCourier): nothing here reads the clock or the network,
// and MakeRoom is the one function that touches the report files.
//
// A report is a "problem" report: the intake makes its comment the Bugsink
// event's message, and Bugsink groups problems by the message's first line,
// so that line is a fixed title per build and per kind of copy (a fighter's,
// or a watcher's, whose mismatch is a diagnostic only). The rest of the
// comment names the match, both picks and the checkpoint, enough to pair the
// two players' reports. The full list of the checkpoint's region hashes and
// the tail of the mod's log go in the report's log, sfv_ember.log: the
// region lines are those LogCheckpointRegions writes, so the two players'
// lists compare line by line (tools/runcross.py reads the same form).

#include "common/sf4e__NetUtil.hxx"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::adapter::desync {

// A fighter as the room's match launched it; -1 where it is not known.
struct FighterFacts {
	std::string code; // the game's code, "RYU"; empty when not known
	int costume = -1;
	int colour = -1;
	int vSkill = -1;   // choice number: 0 is I, 1 is II
	int vTrigger = -1;
};

// A region's hash as the peers compare it.
struct RegionFacts {
	std::string name;
	std::uint64_t size = 0;
	std::uint64_t hash = 0;
};

// A region of this copy's whole state at the checkpoint (StateFile).
struct StateRegionFacts {
	std::string name;
	std::uint64_t size = 0;
	std::uint64_t address = 0;
};

// Everything the game knows of a desync as it finds it.
struct Facts {
	// This copy: its match slot (0 P1, 1 P2, 2 and up watch; -1 not known),
	// and whether it watches.
	int slot = -1;
	bool watcher = false;
	// The room's match: the room's epoch, the table, the match's generation
	// (both players' copies share all three; 0 or -1 where not known), and
	// the games of this battle ended so far.
	std::uint64_t roomEpoch = 0;
	int table = -1;
	std::uint64_t generation = 0;
	unsigned gamesEnded = 0;
	bool picksKnown = false;
	FighterFacts fighters[2]; // P1, P2
	std::string stage;        // the stage's code; empty when not known
	// The checkpoint whose overall hash differs, both hashes, and the
	// session around it.
	int frame = 0;
	std::uint64_t local = 0;
	std::uint64_t remote = 0;
	int confirmed = 0;
	int delay = -1;
	std::uint64_t rollbacks = 0;
	int longestRollback = 0;
	std::uint64_t layoutId = 0;
	std::string matchBuild; // SfvCatalog's MatchBuildId
	std::vector<RegionFacts> regions; // the checkpoint's, in canonical order
	// This copy's whole state at the checkpoint (the session keeps the last
	// two checkpoints'), its regions in the layout's order; empty when it was
	// no longer kept.
	std::vector<StateRegionFacts> stateRegions;
	std::vector<std::uint8_t> state;
};

// A copy's state at the desync's checkpoint, kept on this PC beside the
// report (not sent: about 1 MB), so the two players' files name the bytes
// that differ (tools/diffstate.py). The newest kStatesKept are kept.
constexpr const wchar_t* kStateFolder = L"desync-states";
constexpr const wchar_t* kStateExtension = L".state";
constexpr std::size_t kStatesKept = 8;
// The file: "SFVSTATE", then version 1 and the region count (u32 each), then
// each region: its name's length (u16), the name, its size and address (u64)
// and its bytes. Empty when the regions do not account for the state's bytes.
std::string StateFile(const Facts& facts);

// The intake's limits a report keeps to (the library's ProblemReport.hxx).
constexpr std::size_t kCommentCharacters = 2000;
constexpr std::size_t kLogBytes = 192 * 1024;
// The mod's log lines before the desync a report carries.
constexpr std::size_t kTailLines = 300;

// The comment's first line, which Bugsink groups by: "SFV desync: build
// <first 12 digits of the build id>", or "SFV desync seen by a watcher: ...".
std::string Title(bool watcher, std::string_view buildId);
// The whole comment: the title, then the match, this copy, both picks, the
// checkpoint and the session, at most kCommentCharacters characters.
std::string Comment(const Facts& facts, std::string_view buildId);
// One line per region, "desync: checkpoint F region R, N bytes, hash H".
std::string RegionLines(const Facts& facts);

// The last `lines` lines of the mod's log before the report was written,
// leaving out the region lines of checkpoint `frame` (RegionLines has them).
// `cut`: the text starts somewhere inside the log, so its first line is
// partial and goes.
std::string TailLines(std::string_view log, bool cut, int frame, std::size_t lines = kTailLines);

// The report's log: the comment, the region lines, then as much of the log's
// (already redacted) tail as fits in kLogBytes, its oldest lines going first.
std::string Record(std::string_view comment, std::string_view regionLines, std::string_view redactedTail);

// What the game writes down for the launcher (DesyncReporter, DesyncCourier):
// one JSON file per desync.
struct Spooled {
	std::int64_t time = 0; // unix seconds, when it was written
	std::string appVersion, buildId, sourceRevision;
	bool watcher = false;
	std::string comment, regionLines, logTail; // the tail as the log has it, not redacted
};
std::string SpoolJson(const Spooled& spooled);
// The file's report, or nothing when it is not one this code writes (a
// field missing or of the wrong kind, a build id that is not 64 hex digits,
// an empty or too long comment).
std::optional<Spooled> ParseSpool(std::string_view text);
// The folder the reports wait in, under the product's settings folder.
constexpr const wchar_t* kSpoolFolder = L"desync-reports";
constexpr const wchar_t* kSpoolExtension = L".desync.json";
// At most this many wait (a report is about 75 KB, so about 2.5 MB). A desync
// past them is still written down: the oldest waiting reports go to make room
// (MakeRoom), so the newest are the ones kept. The launcher's daily sending
// limit is separate and stays; it decides what is sent, this what is kept.
constexpr std::size_t kSpoolLimit = 32;
// One older than this is never sent: its build is likely gone.
constexpr std::int64_t kSpoolMaxAgeSeconds = 7 * 24 * 60 * 60;

// What MakeRoom did to the folder of waiting reports.
struct Room {
	std::size_t waiting = 0;                 // reports waiting afterwards
	std::vector<std::wstring> removed;       // the file names it removed, oldest first
};
// Brings the folder to fewer than limit waiting reports for one more by
// removing the oldest ones (the names sort by the time they were written). A
// report the launcher holds open is never removed: the launcher opens it
// without sharing deletion until it is settled, so a removal of it fails and
// the next oldest goes instead. When every one is held, waiting stays at
// limit or more and the new report is not written. A missing folder holds
// none.
Room MakeRoom(const std::filesystem::path& folder, std::size_t limit);

// How one upload of a report ended, and what becomes of its file.
enum class Verdict {
	Delivered,  // the intake has it: the file goes
	WithoutLog, // the intake does not take sfv_ember.log yet: send it once more without the log
	Later,      // no answer, a rate limit or a busy intake: the file stays for a later try
	Refused,    // the intake refused the report itself: the file goes
};
struct Outcome {
	Verdict verdict = Verdict::Later;
	std::string id;     // the intake's report id, when it gave a valid one
	std::string reason; // a short code for the record of sent reports (a-z, 0-9, '_'), or empty
};
// `withLog`: the upload carried the report's log.
Outcome Judge(const sf4e::HttpResponse& answer, bool withLog);

} // namespace sfve::adapter::desync
