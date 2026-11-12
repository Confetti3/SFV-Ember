#pragma once

// A lossy, laggy link between the two sessions of a LoopbackPair: each peer
// sends to a relay port of its own, and the relay passes each datagram on to
// the other peer after a delay (a lag plus a random jitter, kept in order),
// or loses it. GGPO only takes a datagram from the address it sends to, so
// what reaches a peer leaves from the relay port that peer sends to.
//
// Single-threaded: Pump moves what has arrived and passes on what is due.
// Call it on every turn of the loop that steps the sessions
// (LoopbackPair::pump).

#include "LoopbackPair.hxx"

#include <winsock2.h>

#include <stdint.h>

#include <deque>
#include <utility>
#include <vector>

namespace sfve::testbed {

class LossyLink {
public:
	struct Shape {
		int lagMs = 4;       // every datagram's least delay
		int jitterMs = 8;    // and up to this much more
		int lossPercent = 5; // datagrams lost in a hundred
		uint32_t seed = 1;   // the losses and the jitter
	};

	// Peer A plays on loopback port `portA`, peer B on `portB`.
	LossyLink(uint16_t portA, uint16_t portB, const Shape& shape)
		: shape_(shape), random_(shape.seed ? shape.seed : 1), buffer_(65536)
	{
		const uint16_t base = LoopbackPortBase();
		forA_ = base;
		forB_ = (uint16_t)(base + 1);
		toB_.from = Open(forA_);
		toB_.via = Open(forB_);
		toB_.port = portB;
		toA_.from = toB_.via;
		toA_.via = toB_.from;
		toA_.port = portA;
	}
	~LossyLink()
	{
		if (toB_.from != INVALID_SOCKET) closesocket(toB_.from);
		if (toB_.via != INVALID_SOCKET) closesocket(toB_.via);
	}
	LossyLink(const LossyLink&) = delete;
	LossyLink& operator=(const LossyLink&) = delete;

	bool Ready() const { return toB_.from != INVALID_SOCKET && toB_.via != INVALID_SOCKET; }
	// The remote port each peer's session sends to.
	uint16_t PortForA() const { return forA_; }
	uint16_t PortForB() const { return forB_; }
	uint64_t Passed() const { return passed_; }
	uint64_t Lost() const { return lost_; }

	void Pump()
	{
		const double now = NowMs();
		Receive(toB_, now);
		Receive(toA_, now);
		Deliver(toB_, now);
		Deliver(toA_, now);
	}

	static double NowMs()
	{
		LARGE_INTEGER t, perSecond;
		QueryPerformanceCounter(&t);
		QueryPerformanceFrequency(&perSecond);
		return 1000.0 * (double)t.QuadPart / (double)perSecond.QuadPart;
	}

private:
	struct Datagram {
		double due = 0;
		std::vector<char> bytes;
	};
	// One way: what arrives on `from` leaves from `via` for loopback `port`.
	struct Direction {
		SOCKET from = INVALID_SOCKET;
		SOCKET via = INVALID_SOCKET;
		uint16_t port = 0;
		std::deque<Datagram> queue;
		double lastDue = 0;
	};

	static SOCKET Open(uint16_t port)
	{
		const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == INVALID_SOCKET) return s;
		sockaddr_in at{};
		at.sin_family = AF_INET;
		at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		at.sin_port = htons(port);
		u_long nonBlocking = 1;
		if (bind(s, reinterpret_cast<const sockaddr*>(&at), sizeof(at)) != 0 || ioctlsocket(s, FIONBIO, &nonBlocking) != 0) {
			closesocket(s);
			return INVALID_SOCKET;
		}
		return s;
	}

	uint32_t Random()
	{
		random_ ^= random_ << 13;
		random_ ^= random_ >> 17;
		random_ ^= random_ << 5;
		return random_;
	}

	void Receive(Direction& d, double now)
	{
		if (d.from == INVALID_SOCKET) return;
		for (;;) {
			sockaddr_in sender{};
			int senderLen = sizeof(sender);
			const int n = recvfrom(d.from, buffer_.data(), (int)buffer_.size(), 0, reinterpret_cast<sockaddr*>(&sender),
				&senderLen);
			if (n < 0) {
				// A send to a port nobody listens on any more comes back as a
				// reset on the next receive: not the end of what is queued.
				if (WSAGetLastError() == WSAECONNRESET) continue;
				return;
			}
			if (shape_.lossPercent > 0 && (int)(Random() % 100) < shape_.lossPercent) {
				lost_++;
				continue;
			}
			const double jitter = shape_.jitterMs > 0 ? (double)(Random() % (uint32_t)(shape_.jitterMs + 1)) : 0.0;
			Datagram datagram;
			datagram.due = now + shape_.lagMs + jitter;
			if (datagram.due < d.lastDue) datagram.due = d.lastDue; // in order
			d.lastDue = datagram.due;
			datagram.bytes.assign(buffer_.data(), buffer_.data() + n);
			d.queue.push_back(std::move(datagram));
		}
	}

	void Deliver(Direction& d, double now)
	{
		sockaddr_in to{};
		to.sin_family = AF_INET;
		to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		to.sin_port = htons(d.port);
		while (!d.queue.empty() && d.queue.front().due <= now) {
			const Datagram& datagram = d.queue.front();
			sendto(d.via, datagram.bytes.data(), (int)datagram.bytes.size(), 0, reinterpret_cast<const sockaddr*>(&to),
				sizeof(to));
			passed_++;
			d.queue.pop_front();
		}
	}

	Shape shape_;
	uint32_t random_;
	std::vector<char> buffer_;
	uint16_t forA_ = 0, forB_ = 0;
	Direction toB_, toA_;
	uint64_t passed_ = 0, lost_ = 0;
};

} // namespace sfve::testbed
