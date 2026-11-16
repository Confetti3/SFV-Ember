#pragma once

// How OpenLog (mod/Log.hxx) picks its file: the first of its candidates that
// can be written.

#include <windows.h>

#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

// One file to try: the usual log, appended to, or a file of the process's
// own, created new (never another process's).
struct LogCandidate {
	std::wstring path; // empty: skipped
	bool own = false;
};
struct OpenedLog {
	HANDLE file = INVALID_HANDLE_VALUE;
	std::wstring path;
	std::wstring failedPath; // the first that failed, with its error
	DWORD error = 0;
};
// Opens the first candidate that can be written, creating its folders.
OpenedLog OpenFirstLog(const std::vector<LogCandidate>& candidates);
// The candidates OpenLog tries for a process, in order: the usual log of its
// instance (sfv_ember.log, sfv_ember.N.log) in logs, a file of its own
// (sfv_ember-<pid>.log) beside it, then under localLogs, then in %TEMP%.
// A folder that is empty is skipped.
std::vector<LogCandidate> LogCandidates(int instance, unsigned long pid, const std::wstring& logs,
	const std::wstring& localLogs, const std::wstring& temp);


// The runs kept: the log of this run, and this many before it (sfv_ember.old1.log
// the latest, up to sfv_ember.old<kLogRunsKept>.log).
constexpr int kLogRunsKept = 5;
// Moves the log of an earlier run out of the way, `path` to <name>.old1<ext>,
// that one to .old2 and so on, the last of `keep` dropped. A log written since
// `runStart` (this process's creation) is this run's, written by another copy
// of the mod or a start line, and stays. Whether it was moved; a file that is
// absent, empty or held by a program that allows no rename stays as it is.
bool RotateLog(const std::wstring& path, int keep, const FILETIME& runStart);
// What OpenLog does to open the log: the usual log (the first candidate that
// is not a process's own file) is first rotated (RotateLog), once, before
// anything is written to it, then the first candidate that can be written is
// opened (OpenFirstLog). `runStart` is this process's creation, so a log
// another copy of the mod in this process has already written stays.
OpenedLog OpenRotatedLog(const std::vector<LogCandidate>& candidates, int keep, const FILETIME& runStart);

// How much one run may write to its log: a run that logs a line per frame
// would fill the player's disk. The line that passes the limit is written,
// then one that says so, and the rest are dropped.
class RunLogCap {
public:
	explicit RunLogCap(uint64_t limit) : limit_(limit) {}
	enum class Take { Write, WriteThenNotice, Drop };
	Take Admit(uint64_t bytes)
	{
		if (stopped_) return Take::Drop;
		written_ += bytes;
		if (written_ <= limit_) return Take::Write;
		stopped_ = true;
		return Take::WriteThenNotice;
	}

private:
	uint64_t limit_;
	uint64_t written_ = 0;
	bool stopped_ = false;
};
constexpr uint64_t kRunLogLimit = 16ull << 20;

} // namespace sfve::mod
