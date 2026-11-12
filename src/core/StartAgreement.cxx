#include "core/StartAgreement.hxx"

#include "core/CoreLog.hxx"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>

#include <string.h>

#include <algorithm>

#include <xxhash.h>

namespace sfve {

namespace start_state {

namespace {

constexpr size_t kHeader = 16;
constexpr size_t kAskSize = kHeader + 12;
constexpr size_t kSummarySize = kHeader + 24;
constexpr size_t kEntriesHeader = kHeader + 8;
constexpr size_t kEntrySize = 24;
constexpr size_t kBytesHeader = kHeader + 16;
constexpr size_t kDoneSize = kHeader + 16;
constexpr size_t kAddressBitBytes = (kChunkBytes / 4 + 7) / 8;
// More regions than any layout lists (SFV's: about 550; the manager's table
// is read up to 4096).
constexpr uint32_t kMostEntries = 16384;
static_assert(kEntriesHeader + kEntriesPerPart * kEntrySize <= kMaxDatagram, "a region list's datagram is too big");
static_assert(kBytesHeader + kChunkBytes + kAddressBitBytes <= kMaxDatagram, "a chunk's datagram is too big");
static_assert(kChunkBytes % 4 == 0, "a chunk starts on an address unit");

class Writer {
public:
	template <class T>
	void Put(T value)
	{
		const size_t at = out_.size();
		out_.resize(at + sizeof(T));
		memcpy(out_.data() + at, &value, sizeof(T));
	}
	void Put(const uint8_t* data, size_t size) { out_.insert(out_.end(), data, data + size); }
	std::vector<uint8_t> Take() { return std::move(out_); }

private:
	std::vector<uint8_t> out_;
};

class Reader {
public:
	Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
	template <class T>
	bool Get(T* out)
	{
		if (size_ - at_ < sizeof(T)) return false;
		memcpy(out, data_ + at_, sizeof(T));
		at_ += sizeof(T);
		return true;
	}
	bool Get(std::vector<uint8_t>* out, size_t size)
	{
		if (size_ - at_ < size) return false;
		out->assign(data_ + at_, data_ + at_ + size);
		at_ += size;
		return true;
	}
	size_t Left() const { return size_ - at_; }

private:
	const uint8_t* data_;
	size_t size_;
	size_t at_ = 0;
};

bool Bit(const std::vector<uint64_t>& bits, size_t i)
{
	return i / 64 < bits.size() && ((bits[i / 64] >> (i % 64)) & 1);
}

} // namespace

uint64_t RegionIdentity(const std::string& name, size_t size, bool stable)
{
	if (!stable) return 0;
	const uint64_t id = XXH3_64bits_withSeed(name.data(), name.size(), (uint64_t)size);
	return id ? id : 1;
}

std::vector<Entry> ListEntries(const StateLayout& layout, const uint8_t* bytes,
	const std::function<bool(const std::string&)>& stableName)
{
	std::vector<RegionHash> hashes;
	layout.PeerRegionHashesSaved(bytes, hashes);
	std::vector<Entry> out;
	out.reserve(hashes.size());
	for (const RegionHash& h : hashes) {
		const StateRegion& r = layout.Regions()[h.region];
		const bool stable = !stableName || stableName(r.name);
		out.push_back({RegionIdentity(r.name, r.size, stable), h.hash, (uint32_t)r.size, (uint32_t)h.region});
	}
	return out;
}

Pairing PairRegions(const std::vector<Entry>& local, const std::vector<Entry>& reference)
{
	Pairing out;
	// Identities seen exactly once on each side pair; everything else is loose.
	std::map<uint64_t, std::vector<size_t>> localIds, referenceIds;
	for (size_t i = 0; i < local.size(); i++) {
		if (local[i].id) localIds[local[i].id].push_back(i);
	}
	for (size_t i = 0; i < reference.size(); i++) {
		if (reference[i].id) referenceIds[reference[i].id].push_back(i);
	}
	std::vector<bool> localPaired(local.size()), referencePaired(reference.size());
	for (const auto& [id, mine] : localIds) {
		const auto theirs = referenceIds.find(id);
		if (mine.size() != 1 || theirs == referenceIds.end() || theirs->second.size() != 1) continue;
		const Entry& l = local[mine[0]];
		const Entry& r = reference[theirs->second[0]];
		if (l.size != r.size) continue;
		localPaired[mine[0]] = referencePaired[theirs->second[0]] = true;
		if (l.hash == r.hash) out.equal++;
		else out.differing.push_back({l.index, r.index, l.size});
	}
	// Loose regions: the same size and hash on both sides are the same state
	// wherever each lists it.
	std::multimap<std::pair<uint32_t, uint64_t>, size_t> looseReference;
	for (size_t i = 0; i < reference.size(); i++) {
		if (!referencePaired[i]) looseReference.emplace(std::make_pair(reference[i].size, reference[i].hash), i);
	}
	for (size_t i = 0; i < local.size(); i++) {
		if (localPaired[i]) continue;
		const auto it = looseReference.find({local[i].size, local[i].hash});
		if (it == looseReference.end()) continue;
		localPaired[i] = referencePaired[it->second] = true;
		looseReference.erase(it);
		out.equal++;
	}
	// What is left pairs only where its size is the only one left on each side.
	std::map<uint32_t, std::vector<size_t>> leftLocal, leftReference;
	for (size_t i = 0; i < local.size(); i++) {
		if (!localPaired[i]) leftLocal[local[i].size].push_back(i);
	}
	for (size_t i = 0; i < reference.size(); i++) {
		if (!referencePaired[i]) leftReference[reference[i].size].push_back(i);
	}
	for (const auto& [size, mine] : leftLocal) {
		const auto theirs = leftReference.find(size);
		if (mine.size() == 1 && theirs != leftReference.end() && theirs->second.size() == 1) {
			out.differing.push_back({local[mine[0]].index, reference[theirs->second[0]].index, size});
			localPaired[mine[0]] = referencePaired[theirs->second[0]] = true;
		}
	}
	for (size_t i = 0; i < local.size(); i++) {
		if (!localPaired[i]) out.unpairedLocal.push_back(local[i].index);
	}
	for (size_t i = 0; i < reference.size(); i++) {
		if (!referencePaired[i]) out.unpairedReference.push_back(reference[i].index);
	}
	return out;
}

size_t ApplyRegion(const StateLayout& layout, size_t region, const uint8_t* reference,
	const std::vector<uint64_t>& referenceAddressUnits, size_t* kept)
{
	const StateRegion& r = layout.Regions().at(region);
	std::vector<uint64_t> own;
	layout.AddressUnits(region, r.ptr, own);
	size_t written = 0, left = 0;
	for (size_t at = 0; at < r.size; at += 4) {
		const size_t n = std::min<size_t>(4, r.size - at);
		if (memcmp(r.ptr + at, reference + at, n) == 0) continue;
		const size_t unit = at / 4;
		const bool address = Bit(own, unit) || Bit(referenceAddressUnits, unit);
		const bool masked = Bit(r.ignore, at / 8) || Bit(r.peerIgnore, unit);
		if (address || masked) {
			left++;
			continue;
		}
		memcpy(r.ptr + at, reference + at, n);
		written++;
	}
	if (kept) *kept = left;
	return written;
}

std::vector<uint8_t> Encode(const Message& m)
{
	Writer w;
	w.Put<uint16_t>(kMagic);
	w.Put<uint16_t>(kVersion);
	w.Put<uint8_t>((uint8_t)m.type);
	w.Put<uint8_t>(0);
	w.Put<uint16_t>(0);
	w.Put<uint64_t>(m.token);
	switch (m.type) {
	case Type::Ask:
		w.Put<uint8_t>((uint8_t)m.asked);
		w.Put<uint8_t>(0);
		w.Put<uint16_t>(0);
		w.Put<uint32_t>(m.a);
		w.Put<uint32_t>(m.b);
		break;
	case Type::Summary:
		w.Put<uint64_t>(m.layoutId);
		w.Put<uint64_t>(m.hash);
		w.Put<uint32_t>(m.entryCount);
		w.Put<uint32_t>(0);
		break;
	case Type::Entries:
		w.Put<uint32_t>(m.part);
		w.Put<uint16_t>((uint16_t)std::min(m.entries.size(), kEntriesPerPart));
		w.Put<uint16_t>(0);
		for (size_t i = 0; i < m.entries.size() && i < kEntriesPerPart; i++) {
			w.Put<uint64_t>(m.entries[i].id);
			w.Put<uint64_t>(m.entries[i].hash);
			w.Put<uint32_t>(m.entries[i].size);
			w.Put<uint32_t>(m.entries[i].index);
		}
		break;
	case Type::Bytes: {
		const size_t n = std::min(m.bytes.size(), kChunkBytes);
		const size_t bits = std::min(m.addressBits.size(), kAddressBitBytes);
		w.Put<uint32_t>(m.region);
		w.Put<uint32_t>(m.offset);
		w.Put<uint32_t>(m.regionSize);
		w.Put<uint16_t>((uint16_t)n);
		w.Put<uint16_t>((uint16_t)bits);
		w.Put(m.bytes.data(), n);
		w.Put(m.addressBits.data(), bits);
		break;
	}
	case Type::Done:
		w.Put<uint8_t>(m.result);
		for (int i = 0; i < 7; i++) w.Put<uint8_t>(0);
		w.Put<uint64_t>(m.hash);
		break;
	case Type::DoneAck:
		break;
	}
	return w.Take();
}

bool Decode(const uint8_t* data, size_t size, uint64_t token, Message* out)
{
	if (!data || size < kHeader || size > kMaxDatagram) return false;
	Reader r(data, size);
	uint16_t magic = 0, version = 0, pad16 = 0;
	uint8_t type = 0, pad8 = 0;
	uint64_t theirs = 0;
	if (!r.Get(&magic) || !r.Get(&version) || !r.Get(&type) || !r.Get(&pad8) || !r.Get(&pad16) || !r.Get(&theirs)) return false;
	if (magic != kMagic || version != kVersion || theirs != token) return false;
	Message m;
	m.token = theirs;
	m.type = (Type)type;
	switch (m.type) {
	case Type::Ask: {
		if (size != kAskSize) return false;
		uint8_t asked = 0;
		r.Get(&asked);
		r.Get(&pad8);
		r.Get(&pad16);
		r.Get(&m.a);
		r.Get(&m.b);
		if (asked < (uint8_t)Asked::Summary || asked > (uint8_t)Asked::Bytes) return false;
		m.asked = (Asked)asked;
		break;
	}
	case Type::Summary: {
		if (size != kSummarySize) return false;
		uint32_t pad32 = 0;
		r.Get(&m.layoutId);
		r.Get(&m.hash);
		r.Get(&m.entryCount);
		r.Get(&pad32);
		if (m.entryCount > kMostEntries) return false;
		break;
	}
	case Type::Entries: {
		uint16_t count = 0;
		if (size < kEntriesHeader || !r.Get(&m.part) || !r.Get(&count) || !r.Get(&pad16)) return false;
		if (count > kEntriesPerPart || r.Left() != count * kEntrySize) return false;
		m.entries.resize(count);
		for (Entry& e : m.entries) {
			r.Get(&e.id);
			r.Get(&e.hash);
			r.Get(&e.size);
			r.Get(&e.index);
		}
		break;
	}
	case Type::Bytes: {
		uint16_t n = 0, bits = 0;
		if (size < kBytesHeader || !r.Get(&m.region) || !r.Get(&m.offset) || !r.Get(&m.regionSize) || !r.Get(&n) || !r.Get(&bits)) {
			return false;
		}
		if (n == 0 || n > kChunkBytes || bits != (n / 4 + (n % 4 ? 1 : 0) + 7) / 8 || r.Left() != (size_t)n + bits) return false;
		if (m.offset % kChunkBytes != 0 || m.offset >= m.regionSize || n != std::min<size_t>(kChunkBytes, m.regionSize - m.offset)) {
			return false;
		}
		r.Get(&m.bytes, n);
		r.Get(&m.addressBits, bits);
		break;
	}
	case Type::Done: {
		if (size != kDoneSize) return false;
		r.Get(&m.result);
		for (int i = 0; i < 7; i++) r.Get(&pad8);
		r.Get(&m.hash);
		break;
	}
	case Type::DoneAck:
		if (size != kHeader) return false;
		break;
	default:
		return false;
	}
	*out = std::move(m);
	return true;
}

} // namespace start_state

// --- UdpStartChannel ---------------------------------------------------------

UdpStartChannel::~UdpStartChannel()
{
	Close();
}

bool UdpStartChannel::Open(uint16_t localPort, const std::string& remoteIp, const std::vector<uint16_t>& peerPorts)
{
	Close();
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		CoreLog("start: no Winsock (%d); no start state agreement", WSAGetLastError());
		return false;
	}
	started_ = true;
	IN_ADDR remote{};
	if (inet_pton(AF_INET, remoteIp.c_str(), &remote) != 1) {
		CoreLog("start: the peer's address %s does not read; no start state agreement", remoteIp.c_str());
		Close();
		return false;
	}
	ip_ = remote.s_addr;
	ports_ = peerPorts;
	const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		CoreLog("start: no socket (%d); no start state agreement", WSAGetLastError());
		Close();
		return false;
	}
	socket_ = (uintptr_t)s;
	// A loopback peer (the match's bridge, a copy on this PC) is reached from
	// loopback only, as GGPO's own link is.
	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_port = htons(localPort);
	local.sin_addr.s_addr = (ntohl(ip_) >> 24) == 127 ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
	u_long nonBlocking = 1;
	BOOL reportReset = FALSE;
	DWORD returned = 0;
	if (bind(s, reinterpret_cast<const sockaddr*>(&local), (int)sizeof(local)) != 0 || ioctlsocket(s, FIONBIO, &nonBlocking) != 0) {
		CoreLog("start: port %u could not be bound (%d); no start state agreement", localPort, WSAGetLastError());
		Close();
		return false;
	}
	// A peer not listening yet answers with ICMP, which Windows would report
	// on the next receive as a reset: not an error here.
	WSAIoctl(s, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), nullptr, 0, &returned, nullptr, nullptr);
	return true;
}

void UdpStartChannel::Close()
{
	if (socket_ != ~uintptr_t(0)) {
		closesocket((SOCKET)socket_);
		socket_ = ~uintptr_t(0);
	}
	if (started_) {
		WSACleanup();
		started_ = false;
	}
}

bool UdpStartChannel::Send(size_t peer, const uint8_t* data, size_t size)
{
	if (socket_ == ~uintptr_t(0) || peer >= ports_.size() || !ports_[peer]) return false;
	sockaddr_in to{};
	to.sin_family = AF_INET;
	to.sin_port = htons(ports_[peer]);
	to.sin_addr.s_addr = ip_;
	return sendto((SOCKET)socket_, reinterpret_cast<const char*>(data), (int)size, 0, reinterpret_cast<const sockaddr*>(&to),
			   (int)sizeof(to)) == (int)size;
}

bool UdpStartChannel::Receive(size_t* peer, std::vector<uint8_t>* data)
{
	if (socket_ == ~uintptr_t(0)) return false;
	uint8_t buffer[2048];
	for (int tries = 0; tries < 64; tries++) {
		sockaddr_in from{};
		int fromLength = (int)sizeof(from);
		const int got = recvfrom((SOCKET)socket_, reinterpret_cast<char*>(buffer), (int)sizeof(buffer), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLength);
		if (got == SOCKET_ERROR) {
			const int error = WSAGetLastError();
			if (error == WSAECONNRESET || error == WSAEMSGSIZE) continue;
			return false; // WSAEWOULDBLOCK: nothing waiting
		}
		const auto it = std::find(ports_.begin(), ports_.end(), ntohs(from.sin_port));
		if (it == ports_.end() || from.sin_addr.s_addr != ip_) continue; // not a peer's
		*peer = (size_t)(it - ports_.begin());
		data->assign(buffer, buffer + got);
		return true;
	}
	return false;
}

// --- StartAgreement ----------------------------------------------------------

using namespace start_state;

const char* StartAgreement::ResultName(Result result)
{
	switch (result) {
	case Result::Running: return "running";
	case Result::Agreed: return "the same state";
	case Result::Repaired: return "repaired";
	case Result::Differs: return "still differs";
	case Result::LayoutsDiffer: return "another layout";
	case Result::TimedOut: return "no answer in time";
	}
	return "?";
}

StartAgreement::StartAgreement(GameAdapter& game, StartChannel& channel, Config config)
	: game_(game), channel_(channel), config_(std::move(config))
{
}

void StartAgreement::Begin(uint64_t nowMs, size_t peers)
{
	const StateLayout& layout = game_.Layout();
	begunMs_ = nowMs;
	finished_ = false;
	report_ = {};
	state_.resize(layout.TotalSize());
	layout.SaveTo(state_.data());
	offsets_.clear();
	size_t at = 0;
	for (const StateRegion& r : layout.Regions()) {
		offsets_.push_back(at);
		at += r.size;
	}
	entries_ = ListEntries(layout, state_.data(), config_.stableName);
	layoutId_ = layout.LayoutId();
	report_.localBefore = layout.HashSaved(state_.data(), HashScope::Peers);
	report_.localAfter = report_.localBefore;
	if (config_.role == Role::Reference) {
		report_.reference = report_.localBefore;
		report_.peers.assign(peers, Result::Running);
		requiredDoneMs_ = 0;
		addressUnits_.clear();
		CoreLog("start: this copy's start state is the reference (hash %016llx, %zu regions compared); %zu peer(s) "
			"compare theirs with it", (unsigned long long)report_.localBefore, entries_.size(), peers);
		return;
	}
	report_.stateBefore = state_;
	step_ = Step::Summary;
	referenceEntries_.clear();
	partReceived_.clear();
	wanted_.clear();
	asked_.clear();
	doneSince_ = 0;
	doneAcked_ = false;
	CoreLog("start: this copy's start state hash %016llx (%zu regions compared); asking P1 for its own",
		(unsigned long long)report_.localBefore, entries_.size());
}

bool StartAgreement::Pump(uint64_t nowMs)
{
	if (finished_) return true;
	if (config_.role == Role::Reference) PumpReference(nowMs);
	else PumpFollower(nowMs);
	return finished_;
}

void StartAgreement::Send(size_t peer, const Message& m)
{
	const std::vector<uint8_t> bytes = Encode(m);
	channel_.Send(peer, bytes.data(), bytes.size());
}

// --- the reference ---------------------------------------------------------

void StartAgreement::PumpReference(uint64_t nowMs)
{
	size_t peer = 0;
	std::vector<uint8_t> data;
	for (int handled = 0; handled < 512 && channel_.Receive(&peer, &data); handled++) {
		Message m;
		if (!Decode(data.data(), data.size(), config_.token, &m)) continue;
		if (m.type == Type::Ask) {
			Answer(peer, m);
		} else if (m.type == Type::Done && peer < report_.peers.size()) {
			const bool known = m.result >= (uint8_t)Result::Agreed && m.result <= (uint8_t)Result::TimedOut;
			const Result said = known ? (Result)m.result : Result::Differs;
			if (report_.peers[peer] == Result::Running) {
				CoreLog("start: peer %zu (%s) is done: %s, its hash %016llx%s", peer, peer < config_.requiredPeers ? "P2" : "a watcher",
					ResultName(said), (unsigned long long)m.hash, m.hash == report_.localBefore ? ", this copy's" : "");
			}
			report_.peers[peer] = said;
			Message ack;
			ack.type = Type::DoneAck;
			ack.token = config_.token;
			Send(peer, ack);
		}
	}
	const auto done = [&](size_t from, size_t to) {
		for (size_t i = from; i < to && i < report_.peers.size(); i++) {
			if (report_.peers[i] == Result::Running) return false;
		}
		return true;
	};
	const size_t required = std::min(config_.requiredPeers, report_.peers.size());
	if (done(0, required) && !requiredDoneMs_) requiredDoneMs_ = nowMs ? nowMs : 1;
	const bool allDone = done(0, report_.peers.size());
	const bool graceOver = requiredDoneMs_ && nowMs - requiredDoneMs_ >= config_.watcherGraceMs;
	const bool timeUp = nowMs - begunMs_ >= config_.timeLimitMs;
	if (!allDone && !graceOver && !timeUp) return;
	Result result = Result::Agreed;
	if (!done(0, required)) result = Result::TimedOut;
	else if (required) result = report_.peers[0];
	for (size_t i = required; i < report_.peers.size(); i++) {
		if (report_.peers[i] == Result::Running) CoreLog("start: watcher peer %zu did not finish; it starts as it is", i);
	}
	Finish(result, nowMs);
}

void StartAgreement::Answer(size_t peer, const Message& m)
{
	const StateLayout& layout = game_.Layout();
	Message reply;
	reply.token = config_.token;
	switch (m.asked) {
	case Asked::Summary:
		reply.type = Type::Summary;
		reply.layoutId = layoutId_;
		reply.hash = report_.localBefore;
		reply.entryCount = (uint32_t)entries_.size();
		break;
	case Asked::Entries: {
		const size_t from = (size_t)m.a * kEntriesPerPart;
		if (from >= entries_.size()) return;
		reply.type = Type::Entries;
		reply.part = m.a;
		reply.entries.assign(entries_.begin() + (ptrdiff_t)from,
			entries_.begin() + (ptrdiff_t)std::min(entries_.size(), from + kEntriesPerPart));
		break;
	}
	case Asked::Bytes: {
		// Only a region the peers compare, and only a chunk's start.
		if (m.a >= layout.Regions().size()) return;
		const StateRegion& r = layout.Regions()[m.a];
		if (!r.checked || !r.comparedWithPeers || m.b >= r.size || m.b % kChunkBytes != 0) return;
		auto units = addressUnits_.find(m.a);
		if (units == addressUnits_.end()) {
			units = addressUnits_.emplace(m.a, std::vector<uint64_t>()).first;
			layout.AddressUnits(m.a, state_.data() + offsets_[m.a], units->second);
		}
		const size_t n = std::min<size_t>(kChunkBytes, r.size - m.b);
		reply.type = Type::Bytes;
		reply.region = m.a;
		reply.offset = m.b;
		reply.regionSize = (uint32_t)r.size;
		const uint8_t* from = state_.data() + offsets_[m.a] + m.b;
		reply.bytes.assign(from, from + n);
		const size_t unitCount = n / 4 + (n % 4 ? 1 : 0);
		reply.addressBits.assign((unitCount + 7) / 8, 0);
		for (size_t k = 0; k < unitCount; k++) {
			const size_t unit = m.b / 4 + k;
			if (unit / 64 < units->second.size() && ((units->second[unit / 64] >> (unit % 64)) & 1)) {
				reply.addressBits[k / 8] |= (uint8_t)(1u << (k % 8));
			}
		}
		break;
	}
	}
	Send(peer, reply);
}

// --- a follower ------------------------------------------------------------

void StartAgreement::PumpFollower(uint64_t nowMs)
{
	size_t peer = 0;
	std::vector<uint8_t> data;
	for (int handled = 0; handled < 512 && channel_.Receive(&peer, &data); handled++) {
		Message m;
		if (peer != 0 || !Decode(data.data(), data.size(), config_.token, &m)) continue;
		OnFollowerMessage(m, nowMs);
		if (finished_) return;
	}
	if (step_ != Step::Done && nowMs - begunMs_ >= config_.timeLimitMs) {
		CoreLog("start: P1 did not answer everything within %llu ms", (unsigned long long)config_.timeLimitMs);
		Finish(Result::TimedOut, nowMs);
		return;
	}
	AskDue(nowMs);
}

void StartAgreement::OnFollowerMessage(const Message& m, uint64_t nowMs)
{
	const auto answered = [&](Asked asked, uint32_t a, uint32_t b) { asked_.erase({(uint8_t)asked, a, b}); };
	switch (m.type) {
	case Type::Summary: {
		if (step_ != Step::Summary) return;
		answered(Asked::Summary, 0, 0);
		report_.reference = m.hash;
		if (m.layoutId != layoutId_) {
			CoreLog("start: P1's layout %016llx is not this copy's %016llx: nothing compared", (unsigned long long)m.layoutId,
				(unsigned long long)layoutId_);
			Finish(Result::LayoutsDiffer, nowMs);
			return;
		}
		if (m.hash == report_.localBefore) {
			Finish(Result::Agreed, nowMs);
			return;
		}
		CoreLog("start: P1's start state hash %016llx differs from this copy's; comparing its %u regions", (unsigned long long)m.hash,
			m.entryCount);
		if (m.entryCount == 0) {
			Finish(Result::Differs, nowMs);
			return;
		}
		referenceEntries_.assign(m.entryCount, Entry{});
		partReceived_.assign((m.entryCount + kEntriesPerPart - 1) / kEntriesPerPart, false);
		step_ = Step::Entries;
		return;
	}
	case Type::Entries: {
		if (step_ != Step::Entries || m.part >= partReceived_.size() || partReceived_[m.part]) return;
		const size_t from = (size_t)m.part * kEntriesPerPart;
		const size_t expected = std::min(kEntriesPerPart, referenceEntries_.size() - from);
		if (m.entries.size() != expected) return;
		answered(Asked::Entries, m.part, 0);
		std::copy(m.entries.begin(), m.entries.end(), referenceEntries_.begin() + (ptrdiff_t)from);
		partReceived_[m.part] = true;
		if (std::find(partReceived_.begin(), partReceived_.end(), false) != partReceived_.end()) return;

		const Pairing pairing = PairRegions(entries_, referenceEntries_);
		const std::vector<StateRegion>& regions = game_.Layout().Regions();
		for (const uint32_t i : pairing.unpairedLocal) {
			report_.unpaired.push_back(regions[i].name);
			CoreLog("start: region %s (%zu bytes) pairs with none of P1's; left as it is", regions[i].name.c_str(), regions[i].size);
		}
		for (const uint32_t i : pairing.unpairedReference) {
			report_.unpaired.push_back("P1's region " + std::to_string(i));
			CoreLog("start: P1's region %u pairs with none of this copy's", i);
		}
		for (const Pairing::Pair& p : pairing.differing) {
			if (p.local >= regions.size() || p.size != regions[p.local].size || p.size == 0) continue;
			Wanted w;
			w.local = p.local;
			w.reference = p.reference;
			w.size = p.size;
			w.bytes.assign(p.size, 0);
			w.addressUnits.assign((p.size / 4 + 64) / 64, 0);
			w.missing = (p.size + kChunkBytes - 1) / kChunkBytes;
			w.chunks.assign(w.missing, false);
			wanted_.push_back(std::move(w));
		}
		CoreLog("start: %zu region(s) equal, %zu differ, %zu of this copy's and %zu of P1's unpaired", pairing.equal, wanted_.size(),
			pairing.unpairedLocal.size(), pairing.unpairedReference.size());
		if (wanted_.empty()) {
			Finish(Result::Differs, nowMs);
			return;
		}
		step_ = Step::Bytes;
		return;
	}
	case Type::Bytes: {
		if (step_ != Step::Bytes) return;
		for (Wanted& w : wanted_) {
			if (w.reference != m.region) continue;
			const size_t chunk = m.offset / kChunkBytes;
			if (m.regionSize != w.size || chunk >= w.chunks.size() || w.chunks[chunk]) return;
			answered(Asked::Bytes, m.region, m.offset);
			memcpy(w.bytes.data() + m.offset, m.bytes.data(), m.bytes.size());
			const size_t unitCount = m.bytes.size() / 4 + (m.bytes.size() % 4 ? 1 : 0);
			for (size_t k = 0; k < unitCount; k++) {
				if (!((m.addressBits[k / 8] >> (k % 8)) & 1)) continue;
				const size_t unit = m.offset / 4 + k;
				w.addressUnits[unit / 64] |= 1ull << (unit % 64);
			}
			w.chunks[chunk] = true;
			w.missing--;
			break;
		}
		for (const Wanted& w : wanted_) {
			if (w.missing) return;
		}
		Repair();
		Finish(report_.localAfter == report_.reference ? Result::Repaired : Result::Differs, nowMs);
		return;
	}
	case Type::DoneAck:
		if (step_ == Step::Done) doneAcked_ = true;
		return;
	default:
		return;
	}
}

void StartAgreement::AskDue(uint64_t nowMs)
{
	if (step_ == Step::Done) {
		if (finished_) return;
		if (doneAcked_ || nowMs - doneSince_ >= config_.doneLingerMs) {
			finished_ = true;
			return;
		}
		const auto key = std::make_tuple((uint8_t)0, 0u, 0u);
		const auto it = asked_.find(key);
		if (it != asked_.end() && nowMs - it->second < config_.retryMs) return;
		asked_[key] = nowMs;
		Message done;
		done.type = Type::Done;
		done.token = config_.token;
		done.result = (uint8_t)report_.result;
		done.hash = report_.localAfter;
		Send(0, done);
		return;
	}
	size_t open = 0;
	for (const auto& question : asked_) {
		if (nowMs - question.second < config_.retryMs) open++;
	}
	const auto ask = [&](Asked asked, uint32_t a, uint32_t b) {
		const auto key = std::make_tuple((uint8_t)asked, a, b);
		const auto it = asked_.find(key);
		if (it != asked_.end() && nowMs - it->second < config_.retryMs) return true; // still open
		if (open >= config_.window) return false;
		asked_[key] = nowMs;
		open++;
		Message m;
		m.type = Type::Ask;
		m.token = config_.token;
		m.asked = asked;
		m.a = a;
		m.b = b;
		Send(0, m);
		return true;
	};
	switch (step_) {
	case Step::Summary:
		ask(Asked::Summary, 0, 0);
		break;
	case Step::Entries:
		for (uint32_t part = 0; part < partReceived_.size(); part++) {
			if (!partReceived_[part] && !ask(Asked::Entries, part, 0)) break;
		}
		break;
	case Step::Bytes:
		for (const Wanted& w : wanted_) {
			for (uint32_t chunk = 0; chunk < w.chunks.size(); chunk++) {
				if (!w.chunks[chunk] && !ask(Asked::Bytes, w.reference, chunk * (uint32_t)kChunkBytes)) return;
			}
		}
		break;
	case Step::Done:
		break;
	}
}

void StartAgreement::Repair()
{
	const StateLayout& layout = game_.Layout();
	size_t written = 0;
	for (Wanted& w : wanted_) {
		RegionOutcome outcome;
		outcome.name = layout.Regions()[w.local].name;
		outcome.size = w.size;
		for (const Entry& e : entries_) {
			if (e.index == w.local) outcome.before = e.hash;
		}
		for (const Entry& e : referenceEntries_) {
			if (e.index == w.reference) outcome.reference = e.hash;
		}
		outcome.unitsWritten = ApplyRegion(layout, w.local, w.bytes.data(), w.addressUnits, &outcome.unitsKept);
		written += outcome.unitsWritten;
		report_.regions.push_back(std::move(outcome));
	}
	// What a load does after the bytes are back: the game's own fix-ups.
	if (written) game_.AfterLoad();
	if (const char* failure = game_.StateFailure()) CoreLog("start: after the repair the game reports: %s", failure);
	layout.SaveTo(state_.data());
	report_.localAfter = layout.HashSaved(state_.data(), HashScope::Peers);
	const std::vector<Entry> after = ListEntries(layout, state_.data(), config_.stableName);
	for (size_t k = 0; k < report_.regions.size(); k++) {
		RegionOutcome& outcome = report_.regions[k];
		for (const Entry& e : after) {
			if (e.index == wanted_[k].local) outcome.after = e.hash;
		}
		CoreLog("start: repaired %s (%llu bytes): %zu dword(s) taken from P1, %zu differing left as they are (addresses, masked "
				"words); hash %016llx -> %016llx, P1's %016llx%s",
			outcome.name.c_str(), (unsigned long long)outcome.size, outcome.unitsWritten, outcome.unitsKept,
			(unsigned long long)outcome.before, (unsigned long long)outcome.after, (unsigned long long)outcome.reference,
			outcome.after == outcome.reference ? "" : " (still differs)");
	}
}

void StartAgreement::Finish(Result result, uint64_t nowMs)
{
	report_.result = result;
	report_.elapsedMs = nowMs - begunMs_;
	CoreLog("start: %s after %llu ms (this copy %016llx, P1 %016llx)", ResultName(result), (unsigned long long)report_.elapsedMs,
		(unsigned long long)report_.localAfter, (unsigned long long)report_.reference);
	if (config_.role == Role::Reference) {
		finished_ = true;
		return;
	}
	step_ = Step::Done;
	doneSince_ = nowMs;
	asked_.clear();
	AskDue(nowMs); // the first Done now
	// With no answer at all there is nobody to wait for.
	if (result == Result::TimedOut) finished_ = true;
}

} // namespace sfve
