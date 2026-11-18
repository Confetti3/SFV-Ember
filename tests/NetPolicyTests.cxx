#include <doctest/doctest.h>

#include "core/NetPolicy.hxx"

#include <winsock2.h>
#include <ws2tcpip.h>

using namespace sfve::net;

namespace {

sockaddr_in V4(const char* text)
{
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	inet_pton(AF_INET, text, &a.sin_addr);
	a.sin_port = htons(1234);
	return a;
}

sockaddr_in6 V6(const char* text)
{
	sockaddr_in6 a = {};
	a.sin6_family = AF_INET6;
	inet_pton(AF_INET6, text, &a.sin6_addr);
	return a;
}

} // namespace

TEST_CASE("only loopback is reachable by default")
{
	NetPolicy policy;
	auto lo = V4("127.0.0.1"), lo2 = V4("127.4.5.6"), lan = V4("192.168.1.10"), net = V4("8.8.8.8");
	CHECK(policy.AllowsAddress((sockaddr*)&lo, sizeof(lo)));
	CHECK(policy.AllowsAddress((sockaddr*)&lo2, sizeof(lo2)));
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&lan, sizeof(lan)));
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&net, sizeof(net)));

	auto lo6 = V6("::1"), mapped = V6("::ffff:127.0.0.1"), mappedNet = V6("::ffff:8.8.8.8"), net6 = V6("2001:db8::1");
	CHECK(policy.AllowsAddress((sockaddr*)&lo6, sizeof(lo6)));
	CHECK(policy.AllowsAddress((sockaddr*)&mapped, sizeof(mapped)));
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&mappedNet, sizeof(mappedNet)));
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&net6, sizeof(net6)));
}

TEST_CASE("short or unknown addresses are refused")
{
	NetPolicy policy;
	auto lo = V4("127.0.0.1");
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&lo, 4));
	CHECK_FALSE(policy.AllowsAddress(nullptr, 0));
	sockaddr other = {};
	other.sa_family = 32; // AF_BTH
	CHECK_FALSE(policy.AllowsAddress(&other, sizeof(other)));
}

TEST_CASE("an allowed peer is reachable until the peers are cleared")
{
	NetPolicy policy;
	Address peer;
	REQUIRE(ParseNumeric("203.0.113.9", &peer));
	auto a = V4("203.0.113.9");
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&a, sizeof(a)));
	policy.AllowPeer(peer);
	CHECK(policy.AllowsAddress((sockaddr*)&a, sizeof(a)));
	auto mapped = V6("::ffff:203.0.113.9");
	CHECK(policy.AllowsAddress((sockaddr*)&mapped, sizeof(mapped)));
	policy.ClearPeers();
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&a, sizeof(a)));
}

TEST_CASE("name lookups are answered for localhost and allowed numbers only")
{
	NetPolicy policy;
	CHECK(policy.AllowsHostName("localhost"));
	CHECK(policy.AllowsHostName("LocalHost"));
	CHECK(policy.AllowsHostName("127.0.0.1"));
	CHECK(policy.AllowsHostName(nullptr));
	CHECK_FALSE(policy.AllowsHostName("example.com"));
	CHECK_FALSE(policy.AllowsHostName("8.8.8.8"));
	CHECK_FALSE(policy.AllowsHostName("localhost.example.com"));
	CHECK(policy.AllowsHostNameW(L"localhost"));
	CHECK_FALSE(policy.AllowsHostNameW(L"example.com"));
	CHECK_FALSE(policy.AllowsHostNameW(L"löcalhost"));
}

TEST_CASE("this PC's own name may be looked up, other names still not")
{
	NetPolicy policy;
	CHECK_FALSE(policy.AllowsHostName("DESKTOP-TEST"));
	policy.AllowLocalName("DESKTOP-TEST");
	CHECK(policy.AllowsHostName("desktop-test"));
	CHECK(policy.AllowsHostNameW(L"DESKTOP-TEST"));
	CHECK_FALSE(policy.AllowsHostName("desktop-test.example.com"));
}

TEST_CASE("an Ember service is reachable by its name and its learned addresses on 443 only")
{
	NetPolicy policy;
	policy.AllowService("intake.example");
	CHECK(policy.IsServiceName("INTAKE.example"));
	CHECK(policy.AllowsHostName("intake.example"));
	CHECK(policy.AllowsHostNameW(L"Intake.Example"));
	CHECK_FALSE(policy.AllowsHostName("www.intake.example"));
	CHECK_FALSE(policy.AllowsHostName("intake.example.evil.test"));
	CHECK_FALSE(policy.AllowsHostName("capcom.example"));

	// Nothing is reachable until the name's addresses are learned.
	auto a = V4("203.0.113.20"), other = V4("203.0.113.21");
	a.sin_port = htons(NetPolicy::kServicePort);
	other.sin_port = htons(NetPolicy::kServicePort);
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&a, sizeof(a)));
	Address learned;
	REQUIRE(ParseNumeric("203.0.113.20", &learned));
	policy.AllowServiceAddress(learned);
	CHECK(policy.AllowsAddress((sockaddr*)&a, sizeof(a)));
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&other, sizeof(other)));
	auto mapped = V6("::ffff:203.0.113.20");
	mapped.sin6_port = htons(NetPolicy::kServicePort);
	CHECK(policy.AllowsAddress((sockaddr*)&mapped, sizeof(mapped)));

	// Only the HTTPS port, and a numeric lookup of the address stays refused.
	a.sin_port = htons(80);
	CHECK_FALSE(policy.AllowsAddress((sockaddr*)&a, sizeof(a)));
	CHECK_FALSE(policy.AllowsHostName("203.0.113.20"));
}

TEST_CASE("without a service the policy refuses every name but localhost, as before")
{
	NetPolicy policy;
	CHECK_FALSE(policy.IsServiceName("embernetplay.link"));
	CHECK_FALSE(policy.AllowsHostName("embernetplay.link"));
	CHECK_FALSE(policy.IsServiceName(nullptr));
}
