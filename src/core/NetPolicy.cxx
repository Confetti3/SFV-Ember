#include "core/NetPolicy.hxx"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string.h>

#include <algorithm>
#include <string>

namespace sfve::net {

namespace {

void MapV4(const uint8_t v4[4], Address* out)
{
	out->fill(0);
	(*out)[10] = 0xff;
	(*out)[11] = 0xff;
	memcpy(out->data() + 12, v4, 4);
}

bool IsMappedV4(const Address& a)
{
	for (int i = 0; i < 10; i++) {
		if (a[i]) return false;
	}
	return a[10] == 0xff && a[11] == 0xff;
}

} // namespace

bool ToAddress(const sockaddr* addr, int len, Address* out)
{
	if (!addr || len < (int)sizeof(sockaddr)) return false;
	if (addr->sa_family == AF_INET && len >= (int)sizeof(sockaddr_in)) {
		const auto* in = reinterpret_cast<const sockaddr_in*>(addr);
		MapV4(reinterpret_cast<const uint8_t*>(&in->sin_addr), out);
		return true;
	}
	if (addr->sa_family == AF_INET6 && len >= (int)sizeof(sockaddr_in6)) {
		const auto* in6 = reinterpret_cast<const sockaddr_in6*>(addr);
		memcpy(out->data(), &in6->sin6_addr, 16);
		return true;
	}
	return false;
}

bool IsLoopback(const Address& a)
{
	if (IsMappedV4(a)) return a[12] == 127;
	for (int i = 0; i < 15; i++) {
		if (a[i]) return false;
	}
	return a[15] == 1;
}

bool ParseNumeric(const char* text, Address* out)
{
	if (!text) return false;
	in_addr v4;
	if (inet_pton(AF_INET, text, &v4) == 1) {
		MapV4(reinterpret_cast<const uint8_t*>(&v4), out);
		return true;
	}
	in6_addr v6;
	if (inet_pton(AF_INET6, text, &v6) == 1) {
		memcpy(out->data(), &v6, 16);
		return true;
	}
	return false;
}

void NetPolicy::AllowPeer(const Address& a)
{
	std::lock_guard lock(mutex_);
	if (std::find(peers_.begin(), peers_.end(), a) == peers_.end()) peers_.push_back(a);
}

void NetPolicy::AllowLocalName(const std::string& name)
{
	std::lock_guard lock(mutex_);
	localNames_.push_back(name);
}

void NetPolicy::ClearPeers()
{
	std::lock_guard lock(mutex_);
	peers_.clear();
}

bool NetPolicy::Allows(const Address& a) const
{
	if (IsLoopback(a)) return true;
	std::lock_guard lock(mutex_);
	return std::find(peers_.begin(), peers_.end(), a) != peers_.end();
}

bool NetPolicy::AllowsAddress(const sockaddr* addr, int len) const
{
	Address a;
	// Unknown families (Bluetooth, Hyper-V sockets) are refused too.
	return ToAddress(addr, len, &a) && Allows(a);
}

bool NetPolicy::AllowsHostName(const char* host) const
{
	if (!host) return true; // a null node means a local bind address
	if (_stricmp(host, "localhost") == 0) return true;
	{
		std::lock_guard lock(mutex_);
		for (const std::string& name : localNames_) {
			if (_stricmp(host, name.c_str()) == 0) return true;
		}
	}
	Address a;
	return ParseNumeric(host, &a) && Allows(a);
}

bool NetPolicy::AllowsHostNameW(const wchar_t* host) const
{
	if (!host) return true;
	std::string narrow;
	for (const wchar_t* c = host; *c; c++) {
		if (*c > 0x7f) return false; // non-ASCII is never a numeric address or "localhost"
		narrow += (char)*c;
	}
	return AllowsHostName(narrow.c_str());
}

} // namespace sfve::net
