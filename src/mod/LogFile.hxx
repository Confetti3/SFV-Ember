#pragma once

// How OpenLog (mod/Log.hxx) picks its file: the first of its candidates that
// can be written.

#include <windows.h>

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

} // namespace sfve::mod
