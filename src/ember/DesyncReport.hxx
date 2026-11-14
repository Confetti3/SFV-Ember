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
// so that line is a fixed title per build and per match (the match key, which
// both fighters' copies share): one Bugsink issue holds the two players'
// events of one match's desync and no other's. The rest of the comment names
// the match, both picks, the checkpoint and the session, enough to pair the
// two players' reports and to read the likely cause from one of them. The
// full list of the checkpoint's region hashes, the details (inputs, rollbacks,
// scripts) and the tail of the mod's log go in the report's log,
// sfv_ember.log: the region lines are those LogCheckpointRegions writes, so
// the two players' lists compare line by line (tools/runcross.py reads the
// same form). The report's state file travels beside it when the intake takes
// one (the launcher's DesyncCourier deflates it into a part named "state").

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
	// The layout's flags for it: part of the state hash at all, and of the
	// hash the peers compare (the pointer census reads only the latter).
	bool checked = true;
	bool peers = true;
};

// A stretch of frames in which neither player's input changed (the input
// log, DesyncTrace.hxx): `frames` frames from `first`, the bits the game was
// given for P1 and P2, and the frame's RecordedFrame flags (1 held, 2 a
// fight frame that reads the inputs).
struct InputRun {
	int first = 0;
	int frames = 0;
	std::uint32_t p1 = 0;
	std::uint32_t p2 = 0;
	std::uint8_t flags = 0;
};
// Both players' inputs for the frames [first, end) before the checkpoint, as
// this copy played them. Not known when nothing noted them (the hook that
// feeds DesyncTrace is not wired, or the battle's first frames are gone).
struct InputFacts {
	bool known = false;
	int first = 0;
	int end = 0;
	std::vector<InputRun> runs;
};
// Rollbacks of the session in the frames (from, to]: how many, and the deepest
// (the frames the busiest step re-simulated, which is one rollback's depth
// unless two rollbacks fell in one step).
struct RollbackFacts {
	bool known = false;
	int from = 0;
	int to = 0;
	std::uint64_t count = 0;
	int longest = 0;
};
// What a fighter plays at the checkpoint, read from the kept state
// (ScriptsAt): the script player's script number and frame.
struct ScriptFacts {
	std::string unit; // "CharaUnit[0]"
	int side = -1;    // the unit's side in its ActionUnit run: 0 P1, 1 P2
	int script = -1;
	double frame = 0; // the script frame (16.16 fixed point) as a number
	int facing = 0;   // 1: x mirrored
};

// Everything the game knows of a desync as it finds it.
// A named object with its extent, from the mod's RegionNames (Objects(); the
// extent is the tables' lower bound, not the object's size). Facts carries
// the list as the game thread copied it when the mismatch was noted; the
// census reads that copy and never RegionNames itself.
struct CensusObject {
	std::string name;
	std::uint64_t address = 0;
	std::uint64_t extent = 0;
};

// One stale socket read that was given the live value (mod/SocketReads.hxx's
// Miss), as the report lists it.
struct SocketMissFacts {
	int frame = -1;
	int unit = -1;
	std::uint32_t from = 0; // the call site's rva
	std::string socket;
	std::string reason;     // never-seen pose, other root not certifiable, rebuilding off
};

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
	// The last checkpoint at which both players' hashes agreed before this
	// one (-1: none, the first checkpoint already differs).
	int previousMatch = -1;
	// The session's frame as the mismatch was found (the checkpoint is behind
	// it by the confirmation and the hash's travel); -1 not known.
	int now = -1;
	int delay = -1;
	// The whole battle's rollbacks, and the last kRecentFrames frames'.
	std::uint64_t rollbacks = 0;
	int longestRollback = 0;
	RollbackFacts recent;
	// This battle's socket-read counts (SocketReadsSummary: recorded, served
	// exact, rebuilt, missed with the reason, conflicting); empty when not read.
	std::string socketReads;
	// This battle's pose holds (PoseHoldsSummary: the holds made, those whose
	// replay was fresh, the failures by reason); empty when not read.
	std::string poseHolds;
	// This battle's pace so far (mod/Pace.hxx ReportText: the windows of 600
	// updates, the slowest and the last in updates a second, how many were under
	// 55, the controller removed / added events); empty when not read.
	std::string pace;
	// The latest misses (at most 64) from the kRecentFrames frames before the
	// checkpoint, oldest first, copied on the game thread; `socketMissesKnown`
	// false where the ring was not read.
	std::vector<SocketMissFacts> socketMisses;
	bool socketMissesKnown = false;
	InputFacts inputs;
	std::uint64_t layoutId = 0;
	std::string matchBuild; // SfvCatalog's MatchBuildId
	// The battle's named objects (RegionNames) as of the mismatch, copied on
	// the game thread: the census classes a pointer into one as object+offset.
	std::vector<CensusObject> objects;
	std::vector<RegionFacts> regions; // the checkpoint's, in canonical order
	// This copy's whole state at the checkpoint (the session keeps the last
	// RollbackSession::kCheckpointStatesKept checkpoints'), its regions in the layout's order; empty when it was
	// no longer kept.
	std::vector<StateRegionFacts> stateRegions;
	std::vector<std::uint8_t> state;
	// Not empty: no desync but a repair of the start state before frame 0
	// (core/StartAgreement.hxx), in words: what was taken from P1 and what
	// still differed. `frame` is 0, `local` this copy's hash before the repair
	// and `remote` P1's; `regions` and the state are this copy's before it.
	std::string startRepair;
};

// The frames a report's rollback count and its inputs cover.
constexpr int kRecentFrames = 120;
constexpr int kInputFrames = 180;
// The socket-read misses a report lists (mod::socket_reads::MissRing's size).
constexpr std::size_t kMostSocketMisses = 64;

// The match key both fighters' copies share: "<room epoch>.<table>.<match
// generation>" (16 hex digits, then decimals); empty when the room is not
// known (a start repair outside a room, a test).
std::string MatchKey(const Facts& facts);
// The mismatch itself as both copies name it: "<checkpoint>:<lower hash>-
// <higher hash>", each copy's local and remote hashes swapped, so equal on
// both. Always known, so it pairs reports whose room is not.
std::string PairKey(const Facts& facts);
// The fighters' scripts at the checkpoint, read from the kept state: each
// CharaUnit's action part names the script player it plays (a pointer to a
// timeline object whose registered run is another region of the state), whose
// script number and frame are the run's +0x18 and +0x1C (the frame meter's
// fields, tools/stateregion.py --fighters). Empty when the state is not kept
// or has none of them.
std::vector<ScriptFacts> ScriptsAt(const Facts& facts);

// The pointer census of a state file (StateFile, version 2): for every
// 8-byte word, aligned on its real address, of a region the peers hash that
// passes the mod's address test, where it points and what is there. Two
// copies' censuses say whether their diverged pointers point at the same
// things: the same image offset, the same region and offset, the same named
// object and offset, or the same offset in a heap allocation of the same size
// holding the same 32 bytes.
enum class PointerClass : std::uint8_t {
	Unmapped = 0, // not mapped in this process when the census was taken
	Image = 1,    // in the exe: reference is the rva
	Module = 2,   // in another module: reference the offset in it, refIndex the module's number in modules (its size is its name)
	Region = 3,   // in a region of the saved state: reference the offset, refIndex the region's number
	Object = 4,   // in a named object (RegionNames): reference the offset, refIndex the object's number
	Heap = 5,     // anywhere else mapped: reference the offset from the allocation's base
};
struct CensusWord {
	std::uint32_t region = 0; // the region's number in the file
	std::uint32_t offset = 0; // the word's offset in that region
	std::uint64_t value = 0;
	PointerClass cls = PointerClass::Unmapped;
	bool mapped = false;
	std::uint64_t reference = 0;
	std::uint32_t refIndex = 0;
	std::uint64_t allocBase = 0;  // VirtualQuery's allocation base (0 unmapped)
	std::uint64_t allocSize = 0;  // and the size from it to the end of the allocation
	std::uint64_t targetHash = 0; // of the 32 bytes at the target; 0 when unreadable
};
struct PointerCensus {
	std::vector<CensusObject> objects;
	std::vector<std::string> modules; // the modules words point into (class Module), each "module, <size> bytes": never a file name
	std::uint64_t imageBase = 0;
	std::uint64_t imageSize = 0;
	bool truncated = false; // more words than kCensusWords: the rest are not listed
	std::vector<CensusWord> words;
};
constexpr std::size_t kCensusWords = 65536;
// What the process says of an address: the census's one view of memory, a
// fake in the tests and VirtualQuery and ReadProcessMemory in the game.
struct PointerTarget {
	bool mapped = false;
	std::uint64_t allocBase = 0;
	std::uint64_t allocSize = 0;
	std::string module;           // the image the address lies in, by its file name; empty: not in an image
	std::uint64_t moduleBase = 0;
	std::uint64_t hash = 0;       // of 32 bytes at the address; 0 when not readable
};
class PointerEnv {
public:
	virtual ~PointerEnv() = default;
	virtual PointerTarget Look(std::uint64_t address) = 0;
};
// `looksLikeAddress` is the state hash's test (mod/GameMemory.hxx). The exe is
// the module whose base is `census.imageBase` (set by the caller).
PointerCensus TakeCensus(const Facts& facts, PointerEnv& env, bool (*looksLikeAddress)(std::uint64_t),
	PointerCensus seed = {});

// A copy's state at the desync's checkpoint, kept on this PC beside the
// report (about 1 MB raw, 15 KB deflated), so the two players' files name the
// bytes that differ (tools/diffstate.py). The newest kStatesKept are kept.
// The launcher uploads it with the report when the intake takes one
// (DesyncCourier).
constexpr const wchar_t* kStateFolder = L"desync-states";
constexpr const wchar_t* kStateExtension = L".state";
constexpr std::size_t kStatesKept = 8;
// The file. Version 1 (read by the tools, no longer written): "SFVSTATE",
// then 1 and the region count (u32 each), then each region: its name's length
// (u16), the name, its size and address (u64) and its bytes. Version 2:
//   "SFVSTATE", u32 2, u32 region count
//   each region: u16 name length, name, u64 size, u64 address, u32 flags
//                (bit 0 checked, bit 1 in the peers' hash), the bytes
//   u32 objects, each: u16 name length, name, u64 address, u64 extent
//   u32 modules, each: u16 name length, name
//   u64 image base, u64 image size, u8 truncated
//   u32 words, each (60 bytes): u32 region, u32 offset, u64 value, u8 class,
//                u8 mapped, u16 0, u64 reference, u32 reference index, u32 0,
//                u64 allocation base, u64 allocation size, u64 target hash
// All little-endian. The regions' bytes are as saved at the checkpoint; the
// census describes the targets when the file is written, some frames later
// (the targets of a copy that went on playing). Empty when the regions do not account for
// the state's bytes. Without a census the section after the regions is
// written empty (no objects, modules or words).
std::string StateFile(const Facts& facts, const PointerCensus* census = nullptr);

// The intake's limits a report keeps to (the library's ProblemReport.hxx).
constexpr std::size_t kCommentCharacters = 2000;
constexpr std::size_t kLogBytes = 192 * 1024;
// The mod's log lines before the desync a report carries.
constexpr std::size_t kTailLines = 300;

// The comment's first line, which Bugsink groups by: "SFV desync: build
// <first 12 digits of the build id>", then " match <key>" when the match is
// known (MatchKey), or "SFV desync seen by a watcher: ...".
std::string Title(bool watcher, std::string_view buildId, std::string_view key = {});
// A start state repair's first line (Facts::startRepair): "SFV start state
// repaired: build <12 digits>[ match <key>]", or "SFV start state repaired
// for a watcher: ...", its own Bugsink group.
std::string StartRepairTitle(bool watcher, std::string_view buildId, std::string_view key = {});
// The whole comment: the title, then the match, this copy, both picks, the
// checkpoint and the session, at most kCommentCharacters characters.
std::string Comment(const Facts& facts, std::string_view buildId);
// One line per region, "desync: checkpoint F region R, N bytes, hash H".
std::string RegionLines(const Facts& facts);
// What a person reading one report needs beside the comment and the region
// lines: the pair key, both fighters' picks and scripts, the rollbacks of the
// whole battle and of the last kRecentFrames frames, the socket reads and
// both players' inputs in runs ("inputs P1 <hex> P2 <hex> frames A..B").
std::string DetailLines(const Facts& facts);

// The last `lines` lines of the mod's log before the report was written,
// leaving out the region lines of checkpoint `frame` (RegionLines has them).
// `cut`: the text starts somewhere inside the log, so its first line is
// partial and goes.
std::string TailLines(std::string_view log, bool cut, int frame, std::size_t lines = kTailLines);

// The report's log: the comment, the details, the region lines, then as much
// of the log's (already redacted) tail as fits in kLogBytes, its oldest lines
// going first.
std::string Record(std::string_view comment, std::string_view regionLines, std::string_view redactedTail,
	std::string_view details = {});

// What the game writes down for the launcher (DesyncReporter, DesyncCourier):
// one JSON file per desync.
struct Spooled {
	std::int64_t time = 0; // unix seconds, when it was written
	std::string appVersion, buildId, sourceRevision;
	bool watcher = false;
	std::string comment, regionLines, logTail; // the tail as the log has it, not redacted
	std::string details; // DetailLines; empty in a file an older build wrote
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
// (MakeRoom), so the newest are the ones kept. The launcher's sending limits
// (kSendsPerDay) are separate; they decide what is sent, this what is kept.
constexpr std::size_t kSpoolLimit = 32;
// One older than this is never sent: its build is likely gone.
constexpr std::int64_t kSpoolMaxAgeSeconds = 7 * 24 * 60 * 60;

// The launcher's sending rules. Desync reports are counted apart from the
// crash reports' automatic ones (their own record file, kSentFile), so a
// session's desyncs neither wait behind three crash reports nor use them up.
// The day's cap is a flood guard and covers a long session (2026-11-13: six
// desyncs in one; twelve is two such sessions); the server's own limit is five
// requests from one address in ten minutes (server/ember-reports), so the
// courier sends at most kSendsPerTenMinutes in any ten.
constexpr int kSendsPerDay = 12;
constexpr int kSendsPerTenMinutes = 4;
constexpr const wchar_t* kSentFile = L"sent-desync-reports.json";
// Seconds until one more report may be sent, given the times (unix seconds)
// of the sends counted so far: 0 when it may go now.
std::int64_t SendWaitSeconds(const std::vector<std::int64_t>& sentTimes, std::int64_t now);

// A report waiting in the spool, as the courier lists it.
struct Waiting {
	std::int64_t time = 0;
	std::string buildId;
};
// Which waiting reports are sent, in what order, and which are dropped
// unsent. `installedBuildId`: the build the launcher starts (its
// sfv_ember.dll's SHA-256), empty when it cannot be read, in which case no
// report is dropped for its build. A report of another build than the
// installed one cannot be acted on (the code it names is gone), and one past
// kSpoolMaxAgeSeconds is stale. The rest are sent newest first.
struct SendPlan {
	std::vector<std::size_t> send;       // indexes into the list, newest first
	std::vector<std::size_t> otherBuild; // dropped: not the installed build's
	std::vector<std::size_t> stale;      // dropped: older than kSpoolMaxAgeSeconds
};
SendPlan PlanSends(const std::vector<Waiting>& waiting, std::string_view installedBuildId, std::int64_t now);

// A finished multipart/form-data body (the library's BuildMultipart) with one
// more part before its closing boundary: `name`, `filename`, octet-stream
// `bytes`. Empty when the body does not end with the boundary's closing line
// or the result would pass `limit` bytes.
std::string WithPart(std::string_view body, std::string_view boundary, std::string_view name, std::string_view filename,
	std::string_view bytes, std::size_t limit);

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
	WithoutState, // the intake does not take the state part yet: once more without it
	Later,      // no answer, a rate limit or a busy intake: the file stays for a later try
	Refused,    // the intake refused the report itself: the file goes
};
struct Outcome {
	Verdict verdict = Verdict::Later;
	std::string id;     // the intake's report id, when it gave a valid one
	std::string reason; // a short code for the record of sent reports (a-z, 0-9, '_'), or empty
};
// `withLog`, `withState`: the upload carried the report's log, its state.
Outcome Judge(const sf4e::HttpResponse& answer, bool withLog, bool withState = false);

} // namespace sfve::adapter::desync
