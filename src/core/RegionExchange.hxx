#pragma once

// After a checkpoint mismatch, each copy sends the other its region list for
// that checkpoint (per region its stable identity, the peers' hash and the
// size: start_state::Entry, about 550 regions, 13 KB) and pairs the two lists
// as the start agreement does (start_state::PairRegions: by stable name, else
// by hash, else by a size only one region has; a "game.N" name, a place in one
// process's table, never pairs by name). Each copy then logs and reports only
// the regions that truly differ, instead of every region of the checkpoint
// (docs/design/beta10/desync.md, "Reporting").
//
// It runs once the fight has ended, on the session's own UDP path: GGPO's
// port, after GGPO has closed it (the match's bridge, or the other copy on
// this PC), as the start agreement's datagrams do before GGPO binds it. Each
// side sends every part of its list again and again until it has the other's
// whole list and a datagram of the other's saying it has this one's, or the
// time limit passes. No answer is no list: the caller reports every region
// as before and says why.

#include "core/StartAgreement.hxx"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <vector>

namespace sfve {

namespace region_exchange {

constexpr uint16_t kMagic = 0x5345;
constexpr uint16_t kVersion = 1;
// At GGPO's type byte (StartAgreement.hxx): values GGPO's messages, and the
// start agreement's, never take.
constexpr uint8_t kType = 0xE9;
constexpr size_t kEntriesPerPart = start_state::kEntriesPerPart;

// One datagram: part `part` of `parts` of the sender's list, and whether the
// sender already holds the whole of the receiver's.
struct Part {
	uint64_t token = 0;
	uint32_t part = 0, parts = 0;
	bool haveYours = false;
	std::vector<start_state::Entry> entries;
};
std::vector<uint8_t> Encode(const Part& p);
// Refuses another magic, version, type or token, a part past `parts`, more
// than kEntriesPerPart entries or a size that does not fit them.
bool Decode(const uint8_t* data, size_t size, uint64_t token, Part* out);

} // namespace region_exchange

class RegionExchange {
public:
	struct Config {
		uint64_t token = 0; // the same on both copies for this mismatch (its match and checkpoint)
		uint64_t timeLimitMs = 4000;
		uint64_t resendMs = 150;
	};
	RegionExchange(StartChannel& channel, std::vector<start_state::Entry> local, Config config);

	void Begin(uint64_t nowMs);
	// Sends what is due and takes what arrived; true once finished.
	bool Pump(uint64_t nowMs);
	// The other copy's whole list, once it arrived; this copy's own.
	const std::optional<std::vector<start_state::Entry>>& Theirs() const { return theirs_; }
	const std::vector<start_state::Entry>& Mine() const { return local_; }

private:
	void SendAll();

	StartChannel& channel_;
	std::vector<start_state::Entry> local_;
	Config config_;
	uint64_t begunMs_ = 0, sentMs_ = 0;
	bool finished_ = false;
	bool theyHaveMine_ = false;
	std::vector<start_state::Entry> received_;
	std::vector<bool> partReceived_;
	std::optional<std::vector<start_state::Entry>> theirs_;
};

} // namespace sfve
