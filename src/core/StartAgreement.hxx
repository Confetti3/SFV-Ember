#pragma once

// The start state's agreement (docs/design/beta8/determinism.md): before a
// session's first frame, every copy of one match checks that it starts from
// the state P1 starts from, and a copy that does not takes P1's bytes where
// they may be taken.
//
// Each frame-0 cause of a desync so far was found at checkpoint 60 and fixed
// on its own (EMBER_LIBRARY.md sections 48 and 53): a value one process
// started with that the other did not. This turns the next one into a logged
// repair before frame 0 instead of a desync.
//
// Who takes whose state: P1 is the reference (its session streams to the
// watchers, and its replay is the room's); P2 and every watcher follow it.
// The battle stands still meanwhile (it is bound, reseeded and, in a
// Practice room, given its gauges), so the state compared is the one the
// session saves as frame 0.
//
// The follower asks, the reference answers; the reference keeps no state per
// question, so a lost datagram is simply asked again:
//   1. the reference's summary: its layout id, its peers' hash of the whole
//      state, how many regions it lists. Equal hashes: done, nothing written.
//   2. its region list: per region an identity, the peers' hash, the size and
//      its place in its own layout. A region is named by its owner the same
//      way in every process, so a stable name pairs it; a name that is not
//      stable (SFV's unowned "game.N": a table index, which two processes
//      order differently) pairs only by its hash, or by a size no other such
//      region has. A region listed on one side only, or that pairs with
//      nothing, is logged and left alone.
//   3. the bytes of every paired region whose hash differs, in chunks, each
//      with the reference's address units (StateLayout::AddressUnits).
//   4. the follower writes the reference's bytes over its own except where
//      either side holds an address (its heap's or image's, never the
//      other's), the word is masked (kRunRules and the like: not compared,
//      each process's own) or the dword is left out of the peers' hash (a
//      clock reading: each process's own). Then the game's own post-restore
//      fix-ups run (GameAdapter::AfterLoad), as after a load, and the state
//      is hashed again. The session starts either way; a state that still
//      differs then shows at the first checkpoint, as before.
//   5. the follower says it is done, with its hash, until the reference
//      acknowledges it. The reference starts once P2 is done and each watcher
//      is too (or a short grace after P2), or at the time limit.
//
// The transport is the match's own UDP path, GGPO's ports before GGPO binds
// them (StartChannel; UdpStartChannel): the room's link has no message a
// game may add, and these datagrams cross the same bridge GGPO's do. GGPO's
// port drops any datagram whose type it does not know (the port's
// reject-invalid-udp-messages.patch), so a late one of these after GGPO has
// bound the port is dropped there; each of ours has a type byte GGPO never
// uses, at the place GGPO reads its own, and never exceeds kMaxDatagram, well
// under the most a match's bridge carries (GgpoMaximumPacket).

#include "core/GameAdapter.hxx"

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace sfve {

namespace start_state {

constexpr uint16_t kMagic = 0x5345;
constexpr uint16_t kVersion = 1;
// Every datagram's most bytes (the largest, a region list's or a chunk's, is
// under it; static_asserts in the .cxx).
constexpr size_t kMaxDatagram = 900;
// A chunk of a region's bytes: a multiple of 4, so its address units start
// on its own first byte.
constexpr size_t kChunkBytes = 768;
// Region entries per list datagram.
constexpr size_t kEntriesPerPart = 36;

// At GGPO's type byte (its header: magic u16, sequence u16, type u8), values
// GGPO's messages never take.
enum class Type : uint8_t {
	Ask = 0xE1,
	Summary = 0xE2,
	Entries = 0xE3,
	Bytes = 0xE4,
	Done = 0xE5,
	DoneAck = 0xE6,
};
enum class Asked : uint8_t {
	Summary = 1,
	Entries = 2, // a: the part
	Bytes = 3,   // a: the reference's region index, b: the offset
};

// A region as the reference lists it.
struct Entry {
	uint64_t id = 0;   // RegionIdentity; 0 when its name is not stable
	uint64_t hash = 0; // the peers' hash
	uint32_t size = 0;
	uint32_t index = 0; // its place in the sender's layout
};

// A stable name's identity: the same in every process that names a region
// so; never 0. 0 for a name that is not stable.
uint64_t RegionIdentity(const std::string& name, size_t size, bool stable);

// The regions the peers compare, as `layout` lists them, with `bytes` (laid
// out by SaveTo).
std::vector<Entry> ListEntries(const StateLayout& layout, const uint8_t* bytes,
	const std::function<bool(const std::string&)>& stableName);

// Which local region pairs with which of the reference's: those whose hash
// differs (to repair), and what could not be paired. Pure.
struct Pairing {
	struct Pair {
		uint32_t local = 0;     // this layout's index
		uint32_t reference = 0; // the reference's index
		uint32_t size = 0;
	};
	std::vector<Pair> differing;
	size_t equal = 0;
	std::vector<uint32_t> unpairedLocal;     // local indices
	std::vector<uint32_t> unpairedReference; // the reference's indices
};
Pairing PairRegions(const std::vector<Entry>& local, const std::vector<Entry>& reference);

// Writes `reference` (a region's bytes as the reference holds them, and its
// address units) over region `region` of `layout`'s live memory, unit by
// unit, except where this copy's own bytes hold an address, the reference's
// do, the word is masked or the dword is left out of the peers' hash.
// Returns the units written that differed; `kept` gets the units that
// differ and were left alone.
size_t ApplyRegion(const StateLayout& layout, size_t region, const uint8_t* reference,
	const std::vector<uint64_t>& referenceAddressUnits, size_t* kept);

// Datagrams, encoded and decoded (little-endian). Decode refuses another
// magic, version, token or a size that does not fit its type.
struct Message {
	Type type = Type::Ask;
	uint64_t token = 0;
	// Ask
	Asked asked = Asked::Summary;
	uint32_t a = 0, b = 0;
	// Summary
	uint64_t layoutId = 0, hash = 0;
	uint32_t entryCount = 0;
	// Entries
	uint32_t part = 0;
	std::vector<Entry> entries;
	// Bytes: region (index), offset, regionSize, the bytes and their units'
	// address bits (bit k: unit offset/4 + k).
	uint32_t region = 0, offset = 0, regionSize = 0;
	std::vector<uint8_t> bytes;
	std::vector<uint8_t> addressBits;
	// Done
	uint8_t result = 0;
};
std::vector<uint8_t> Encode(const Message& m);
bool Decode(const uint8_t* data, size_t size, uint64_t token, Message* out);

} // namespace start_state

// A datagram path to each peer (0..n-1). Non-blocking.
class StartChannel {
public:
	virtual ~StartChannel() = default;
	virtual bool Send(size_t peer, const uint8_t* data, size_t size) = 0;
	// The next datagram waiting, from a known peer; false when none.
	virtual bool Receive(size_t* peer, std::vector<uint8_t>* data) = 0;
};

// UDP on the session's local port to each peer's port on loopback (the
// match's bridge, or the other copy on this PC). Closed before GGPO binds
// the port. Open() false (logged) when the port cannot be bound.
class UdpStartChannel final : public StartChannel {
public:
	UdpStartChannel() = default;
	~UdpStartChannel() override;
	UdpStartChannel(const UdpStartChannel&) = delete;
	UdpStartChannel& operator=(const UdpStartChannel&) = delete;

	bool Open(uint16_t localPort, const std::string& remoteIp, const std::vector<uint16_t>& peerPorts);
	void Close();
	bool Send(size_t peer, const uint8_t* data, size_t size) override;
	bool Receive(size_t* peer, std::vector<uint8_t>* data) override;

private:
	uintptr_t socket_ = ~uintptr_t(0); // INVALID_SOCKET
	bool started_ = false;             // WSAStartup's, balanced in Close
	uint32_t ip_ = 0;                  // network order
	std::vector<uint16_t> ports_;
};

class StartAgreement {
public:
	enum class Role { Reference, Follower };
	enum class Result : uint8_t {
		Running = 0,
		Agreed = 1,        // the same state already
		Repaired = 2,      // differed; equal after the repair
		Differs = 3,       // differed; still differs after what could be written
		LayoutsDiffer = 4, // another layout: nothing compared
		TimedOut = 5,      // no answer in time (a reference: P2 never said it was done)
	};
	static const char* ResultName(Result result);

	struct Config {
		Role role = Role::Follower;
		uint64_t token = 0; // the match's (its seed): the same on every copy
		// A reference's peers [0, requiredPeers) must be done (P2); the rest
		// are watchers, waited for only watcherGraceMs after them.
		size_t requiredPeers = 1;
		uint64_t timeLimitMs = 8000;
		uint64_t watcherGraceMs = 1000;
		uint64_t retryMs = 100;
		// How long a follower repeats its Done without an acknowledgement.
		uint64_t doneLingerMs = 1000;
		// Questions a follower keeps open at once.
		size_t window = 16;
		// A region name that names the same region in every process.
		std::function<bool(const std::string&)> stableName;
	};

	// One repaired or still differing region, for the log and the report.
	struct RegionOutcome {
		std::string name;
		uint64_t size = 0;
		uint64_t before = 0; // this copy's peers' hash before the repair
		uint64_t reference = 0;
		uint64_t after = 0;
		size_t unitsWritten = 0;
		size_t unitsKept = 0; // differing, left alone (an address, a mask)
	};
	struct Report {
		Result result = Result::Running;
		uint64_t localBefore = 0; // this copy's peers' hash at Begin
		uint64_t localAfter = 0;
		uint64_t reference = 0;   // the reference's (0 when never heard)
		std::vector<RegionOutcome> regions;
		std::vector<std::string> unpaired; // names on this side; "reference region N" for the other's
		std::vector<uint8_t> stateBefore;  // a follower's whole state at Begin (laid out by SaveTo)
		// A reference: each peer's result as it said it (Running: never said).
		std::vector<Result> peers;
		uint64_t elapsedMs = 0;
	};

	StartAgreement(GameAdapter& game, StartChannel& channel, Config config);
	// The state as it is now is the start state. `peers`: a reference's
	// count (the follower's one peer is the reference).
	void Begin(uint64_t nowMs, size_t peers = 1);
	// Sends and answers what is due; true once finished (Report holds why).
	bool Pump(uint64_t nowMs);
	bool Finished() const { return finished_; }
	const Report& GetReport() const { return report_; }

private:
	void PumpReference(uint64_t nowMs);
	void Answer(size_t peer, const start_state::Message& m);
	void PumpFollower(uint64_t nowMs);
	void OnFollowerMessage(const start_state::Message& m, uint64_t nowMs);
	void AskDue(uint64_t nowMs);
	void Repair();
	void Finish(Result result, uint64_t nowMs);
	void Send(size_t peer, const start_state::Message& m);

	GameAdapter& game_;
	StartChannel& channel_;
	Config config_;
	Report report_;
	bool finished_ = false;
	uint64_t begunMs_ = 0;

	// Both: this copy's start state and its region list.
	std::vector<uint8_t> state_;
	std::vector<start_state::Entry> entries_;
	uint64_t layoutId_ = 0;

	std::vector<size_t> offsets_; // each region's place in state_

	// Reference.
	uint64_t requiredDoneMs_ = 0; // when the last required peer said it was done (0: not yet)
	std::map<uint32_t, std::vector<uint64_t>> addressUnits_; // per region asked for, of state_

	// Follower.
	enum class Step { Summary, Entries, Bytes, Done };
	Step step_ = Step::Summary;
	std::vector<start_state::Entry> referenceEntries_;
	std::vector<bool> partReceived_;
	struct Wanted {
		uint32_t local = 0, reference = 0, size = 0;
		std::vector<uint8_t> bytes;
		std::vector<uint64_t> addressUnits;
		std::vector<bool> chunks;
		size_t missing = 0;
	};
	std::vector<Wanted> wanted_;
	// Open questions: (kind, a, b) -> when last asked.
	std::map<std::tuple<uint8_t, uint32_t, uint32_t>, uint64_t> asked_;
	uint64_t doneSince_ = 0;
	bool doneAcked_ = false;
};

} // namespace sfve
