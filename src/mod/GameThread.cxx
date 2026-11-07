#include "mod/GameThread.hxx"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace sfve::mod {

namespace {

std::mutex s_mutex;
std::vector<std::function<void()>> s_queue;
std::vector<std::function<bool()>> s_repeating; // game thread only, after they are added
std::vector<std::function<bool()>> s_added;
std::atomic<uint32_t> s_gameThreadId;
std::atomic<uint64_t> s_frames;
bool s_pumping; // game thread only

} // namespace

void RunOnGameThread(std::function<void()> work)
{
	std::lock_guard lock(s_mutex);
	s_queue.push_back(std::move(work));
}

void RunEachGameFrame(std::function<bool()> work)
{
	std::lock_guard lock(s_mutex);
	s_added.push_back(std::move(work));
}

uint32_t GameThreadId()
{
	return s_gameThreadId;
}

uint64_t GameFrameNumber()
{
	return s_frames;
}

void PumpGameThread()
{
	// Work that reaches a game function which pumps again would run inside
	// itself and change the list being walked; it waits for the next frame.
	if (s_pumping) return;
	s_pumping = true;
	s_gameThreadId = GetCurrentThreadId();
	s_frames++;
	std::vector<std::function<void()>> work;
	{
		std::lock_guard lock(s_mutex);
		work.swap(s_queue);
		for (auto& w : s_added) s_repeating.push_back(std::move(w));
		s_added.clear();
	}
	for (std::function<void()>& w : work) w();
	std::erase_if(s_repeating, [](std::function<bool()>& w) { return !w(); });
	s_pumping = false;
}

} // namespace sfve::mod
