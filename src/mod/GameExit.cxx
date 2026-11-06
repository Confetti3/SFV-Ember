#include "mod/GameExit.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <utility>
#include <vector>

namespace sfve::mod {

namespace {

using RequestExitFn = void (*)(bool force);

// About one frame between the work's steps.
constexpr DWORD kStepMs = 16;

RequestExitFn o_requestExit;
std::vector<ExitWork> s_work; // game thread only
bool s_ran = false;           // game thread only

// Every work's step once; true when all are done.
bool StepAll()
{
	bool done = true;
	for (ExitWork& work : s_work) {
		if (work.step && !work.step()) done = false;
	}
	return done;
}

void RunWork(bool force)
{
	const ULONGLONG started = GetTickCount64();
	Log("exit: the game asks to exit%s; leaving gracefully first", force ? " at once" : "");
	for (ExitWork& work : s_work) {
		if (work.begin) work.begin();
	}
	bool done = StepAll();
	while (!done && GetTickCount64() - started < kExitWaitMs) {
		Sleep(kStepMs);
		done = StepAll();
	}
	for (ExitWork& work : s_work) {
		if (work.finish) work.finish();
	}
	Log("exit: %s; the engine exits after %llu ms", done ? "the graceful leave is done" : "the graceful leave ran out of time",
		GetTickCount64() - started);
}

void h_RequestExit(bool force)
{
	if (!s_ran) {
		s_ran = true;
		if (GameThreadId() && GetCurrentThreadId() == GameThreadId()) {
			RunWork(force);
		} else {
			// A crash handler or a fatal error elsewhere: the work is the game
			// thread's, and that thread may be the one in trouble.
			Log("exit: the game asks to exit from thread %lu, not the game thread; nothing is left gracefully",
				GetCurrentThreadId());
		}
	}
	o_requestExit(force);
}

} // namespace

bool InstallGameExit(uintptr_t imageBase)
{
	const bool hooked = HookCodeSite(imageBase, sfv::kRequestExit, (void*)&h_RequestExit, (void**)&o_requestExit);
	Log("exit: %s", hooked ? "the engine's exit request is hooked" : "could not hook the engine's exit request");
	return hooked;
}

void OnGameExit(ExitWork work)
{
	s_work.push_back(std::move(work));
}

} // namespace sfve::mod
