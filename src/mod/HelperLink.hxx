#pragma once

// The game's connection to Ember's network helper (sf4-net.exe, next to the
// mod), started and spoken to the way the SF4 launcher and mod do: the
// helper is a child of this process in a kill-on-close job, authenticated
// over its named pipe by a nonce (Ember's HelperProcess/HelperClient,
// src/shared/platform). The helper carries all internet traffic; the game
// process still only talks to loopback (NetGuard).
//
// A worker thread pumps helper events into a MatchLink and its commands back
// to the helper, so a link can be made from the menus before a battle starts.
// Pipe commands: "host" (open a room; the invitation shows in the status) and
// "join <invitation>".

#include "core/MatchLink.hxx"

#include "platform/HelperClient.hxx"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sfve::mod {

class HelperLink {
public:
	explicit HelperLink(int inputDelay);
	~HelperLink();
	HelperLink(const HelperLink&) = delete;
	HelperLink& operator=(const HelperLink&) = delete;

	// Starts the helper and the pump. False (logged) when the helper is
	// missing or does not come up.
	bool Start();
	void Host();
	void Join(const std::string& invitation);

	// The match's endpoints once the link is ready.
	std::optional<MatchLink::Endpoints> Ready() const;
	std::string Describe() const; // JSON for the status report
	void RegisterCommands();

private:
	void Pump();
	void SendCommands(); // with mutex_ held

	const int inputDelay_;
	sf4e::platform::HelperProcess process_;
	sf4e::platform::HelperClient client_;
	mutable std::mutex mutex_; // MatchLink is used by the pump and the pipe threads
	MatchLink link_;
	std::atomic<bool> stop_{false};
	std::thread pump_;
	std::string failure_;
};

} // namespace sfve::mod
