#pragma once

// Which network destinations the game process may reach.
//
// SFV Ember keeps Street Fighter V off Capcom's services entirely: the game
// process may talk to loopback (our own helper process and local tests) and
// to peers the session explicitly allows, nothing else. Name lookups are
// refused for every name but "localhost", so the game cannot even resolve
// Capcom's hosts. The mod's NetGuard hooks ask this policy on every connect,
// send and lookup; the policy itself is pure so it is unit tested.

#include <stdint.h>

#include <array>
#include <mutex>
#include <string>
#include <vector>

struct sockaddr;

namespace sfve::net {

// 16-byte address; IPv4 is stored IPv4-mapped (::ffff:a.b.c.d).
using Address = std::array<uint8_t, 16>;

bool ToAddress(const sockaddr* addr, int len, Address* out);
bool IsLoopback(const Address& a);
bool ParseNumeric(const char* text, Address* out);

class NetPolicy {
public:
	void AllowPeer(const Address& a);
	void ClearPeers();
	// A name that always resolves locally (this PC's own name: UE4 looks it up
	// to find its local addresses). Compared without case.
	void AllowLocalName(const std::string& name);

	bool AllowsAddress(const sockaddr* addr, int len) const;
	// A name lookup. Only "localhost" and numeric addresses the policy would
	// allow anyway are answered.
	bool AllowsHostName(const char* host) const;
	bool AllowsHostNameW(const wchar_t* host) const;

private:
	bool Allows(const Address& a) const;

	mutable std::mutex mutex_;
	std::vector<Address> peers_;
	std::vector<std::string> localNames_;
};

} // namespace sfve::net
