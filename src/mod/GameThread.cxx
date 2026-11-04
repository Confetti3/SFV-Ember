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

std::atomic<uint32_t> s_gameThreadId;

uint32_t GameThreadId() { return s_gameThreadId; }

void PumpGameThread()
{
	s_gameThreadId = GetCurrentThreadId();
	std::vector<std::function<void()>> work;
	{
		std::lock_guard lock(s_mutex);
		work.swap(s_queue);
		for (auto& w : s_added) s_repeating.push_back(std::move(w));
		s_added.clear();
	}
	for (std::function<void()>& w : work) w();
	std::erase_if(s_repeating, [](std::function<bool()>& w) { return !w(); });
}

} // namespace sfve::mod
