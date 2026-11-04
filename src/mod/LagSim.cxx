#include "mod/LagSim.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace sfve::mod {

namespace {

struct Datagram {
	ULONGLONG due;
	SOCKET socket;
	std::string bytes;
	int flags;
	sockaddr_storage to;
	int tolen;
};

int s_delayMs = 0;
SendToFn s_send;
std::mutex s_mutex;
std::condition_variable s_wake;
std::deque<Datagram> s_queue; // in send order; every entry has the same delay, so due times rise

void Sender()
{
	std::unique_lock lock(s_mutex);
	for (;;) {
		s_wake.wait(lock, [] { return !s_queue.empty(); });
		const ULONGLONG now = GetTickCount64();
		if (s_queue.front().due > now) {
			s_wake.wait_for(lock, std::chrono::milliseconds(s_queue.front().due - now));
			continue;
		}
		Datagram d = std::move(s_queue.front());
		s_queue.pop_front();
		lock.unlock();
		s_send(d.socket, d.bytes.data(), (int)d.bytes.size(), d.flags, reinterpret_cast<const sockaddr*>(&d.to), d.tolen);
		lock.lock();
	}
}

} // namespace

void StartLagSim(int delayMs, SendToFn send)
{
	if (delayMs <= 0) return;
	s_delayMs = delayMs;
	s_send = send;
	std::thread(Sender).detach();
	Log("lag sim: outgoing UDP held back %d ms", delayMs);
}

bool LagSimQueue(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen)
{
	if (s_delayMs <= 0 || !to || tolen <= 0 || tolen > (int)sizeof(sockaddr_storage)) return false;
	Datagram d{GetTickCount64() + s_delayMs, s, std::string(buf, len), flags, {}, tolen};
	memcpy(&d.to, to, tolen);
	{
		std::lock_guard lock(s_mutex);
		s_queue.push_back(std::move(d));
	}
	s_wake.notify_one();
	return true;
}

} // namespace sfve::mod
