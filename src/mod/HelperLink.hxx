#pragma once

// The game's connection to Ember's network helper (sf4-net.exe, next to the
// mod), started and spoken to the way the SF4 launcher and mod do: the
// helper is a child of this process in a kill-on-close job, authenticated
// over its named pipe by a nonce (Ember's HelperProcess/HelperClient,
// the library's platform/ folder). The helper carries all internet traffic; the game
// process still only talks to loopback (NetGuard).
//
// A worker thread pumps helper events into a MatchLink and its commands back
// to the helper, so a link can be made from the menus before a battle starts.
// Pipe commands: "host [setup]" (open a room; the invitation shows in the
// status) and "join <invitation> [setup]"; without one, this copy's setup
// from the command line. When the match is ready (linked, both setups known)
// the pump calls onReady once.

#include "core/MatchLink.hxx"

#include "platform/HelperClient.hxx"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sfve::mod {

// The build a match is played on, bound into the room's invitations: a
// different build cannot join.
constexpr const char* kMatchBuild = "sfv-ember/13173604/0.1";

class HelperLink {
public:
	using ReadyFn = std::function<void(const MatchLink::Endpoints&)>;

	// `setup` is this copy's (MatchLink); `onReady` runs on the pump thread.
	HelperLink(int inputDelay, std::string setup, ReadyFn onReady);
	~HelperLink();
	HelperLink(const HelperLink&) = delete;
	HelperLink& operator=(const HelperLink&) = delete;

	// Starts the helper and the pump. False (logged) when the helper is
	// missing or does not come up.
	bool Start();
	// Empty, or why nothing was started (already started, a bad setup).
	std::string Host(std::string setup);
	std::string Join(const std::string& invitation, std::string setup);
	// The match cannot be played as linked (its battle could not be
	// launched): Failed() from now on, so the battle runs offline.
	void Abandon(const std::string& why);

	// The match's endpoints once the link is ready.
	std::optional<MatchLink::Endpoints> Ready() const;
	// The helper did not come up, its connection broke, or the link failed:
	// no match will come from it.
	bool Failed() const;
	std::string Failure() const; // why, without the invitation (a secret)
	std::string Describe() const; // JSON for the status report
	void RegisterCommands();

private:
	void Pump();
	void SendCommands(); // with mutex_ held

	const int inputDelay_;
	const std::string setup_;
	const ReadyFn onReady_;
	sf4e::platform::HelperProcess process_;
	sf4e::platform::HelperClient client_;
	mutable std::mutex mutex_; // MatchLink is used by the pump and the pipe threads
	MatchLink link_;
	std::atomic<bool> stop_{false};
	std::thread pump_;
	std::string failure_;
};

} // namespace sfve::mod
