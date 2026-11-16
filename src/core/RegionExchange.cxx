#include "core/RegionExchange.hxx"

#include <string.h>

#include <algorithm>

namespace sfve {

namespace region_exchange {

namespace {

constexpr size_t kHeader = 16;                        // magic, version, type, pad, pad16, token
constexpr size_t kPartHeader = kHeader + 4 + 4 + 2 + 2; // part, parts, count, flags
constexpr size_t kEntrySize = 8 + 8 + 4 + 4;            // identity, hash, size, index
constexpr uint32_t kMostParts = 1024;
static_assert(kPartHeader + kEntriesPerPart * kEntrySize <= start_state::kMaxDatagram, "a part's datagram is too big");

template <class T>
void Put(std::vector<uint8_t>& out, T value)
{
	const size_t at = out.size();
	out.resize(at + sizeof(T));
	memcpy(out.data() + at, &value, sizeof(T));
}

template <class T>
T Get(const uint8_t*& at)
{
	T value;
	memcpy(&value, at, sizeof(T));
	at += sizeof(T);
	return value;
}

} // namespace

std::vector<uint8_t> Encode(const Part& p)
{
	std::vector<uint8_t> out;
	const size_t count = std::min(p.entries.size(), kEntriesPerPart);
	Put<uint16_t>(out, kMagic);
	Put<uint16_t>(out, kVersion);
	Put<uint8_t>(out, kType);
	Put<uint8_t>(out, 0);
	Put<uint16_t>(out, 0);
	Put<uint64_t>(out, p.token);
	Put<uint32_t>(out, p.part);
	Put<uint32_t>(out, p.parts);
	Put<uint16_t>(out, (uint16_t)count);
	Put<uint16_t>(out, p.haveYours ? 1 : 0);
	for (size_t i = 0; i < count; i++) {
		Put<uint64_t>(out, p.entries[i].id);
		Put<uint64_t>(out, p.entries[i].hash);
		Put<uint32_t>(out, p.entries[i].size);
		Put<uint32_t>(out, p.entries[i].index);
	}
	return out;
}

bool Decode(const uint8_t* data, size_t size, uint64_t token, Part* out)
{
	if (!data || size < kPartHeader || size > start_state::kMaxDatagram) return false;
	const uint8_t* at = data;
	const uint16_t magic = Get<uint16_t>(at), version = Get<uint16_t>(at);
	const uint8_t type = Get<uint8_t>(at);
	at += 3;
	const uint64_t theirs = Get<uint64_t>(at);
	if (magic != kMagic || version != kVersion || type != kType || theirs != token) return false;
	Part p;
	p.token = theirs;
	p.part = Get<uint32_t>(at);
	p.parts = Get<uint32_t>(at);
	const uint16_t count = Get<uint16_t>(at);
	const uint16_t flags = Get<uint16_t>(at);
	if (p.parts == 0 || p.parts > kMostParts || p.part >= p.parts || count > kEntriesPerPart || flags > 1) return false;
	if (size != kPartHeader + count * kEntrySize) return false;
	p.haveYours = flags == 1;
	p.entries.resize(count);
	for (start_state::Entry& e : p.entries) {
		e.id = Get<uint64_t>(at);
		e.hash = Get<uint64_t>(at);
		e.size = Get<uint32_t>(at);
		e.index = Get<uint32_t>(at);
	}
	*out = std::move(p);
	return true;
}

} // namespace region_exchange

using namespace region_exchange;

RegionExchange::RegionExchange(StartChannel& channel, std::vector<start_state::Entry> local, Config config)
	: channel_(channel), local_(std::move(local)), config_(config)
{
}

void RegionExchange::Begin(uint64_t nowMs)
{
	begunMs_ = nowMs;
	sentMs_ = 0;
	finished_ = false;
	theyHaveMine_ = false;
	received_.clear();
	partReceived_.clear();
	theirs_.reset();
	SendAll();
	sentMs_ = nowMs;
}

void RegionExchange::SendAll()
{
	const uint32_t parts = (uint32_t)std::max<size_t>(1, (local_.size() + kEntriesPerPart - 1) / kEntriesPerPart);
	for (uint32_t part = 0; part < parts; part++) {
		Part p;
		p.token = config_.token;
		p.part = part;
		p.parts = parts;
		p.haveYours = theirs_.has_value();
		const size_t from = (size_t)part * kEntriesPerPart;
		if (from < local_.size()) {
			p.entries.assign(local_.begin() + (ptrdiff_t)from, local_.begin() + (ptrdiff_t)std::min(local_.size(), from + kEntriesPerPart));
		}
		const std::vector<uint8_t> bytes = Encode(p);
		channel_.Send(0, bytes.data(), bytes.size());
	}
}

bool RegionExchange::Pump(uint64_t nowMs)
{
	if (finished_) return true;
	size_t peer = 0;
	std::vector<uint8_t> data;
	for (int handled = 0; handled < 512 && channel_.Receive(&peer, &data); handled++) {
		Part p;
		if (peer != 0 || !Decode(data.data(), data.size(), config_.token, &p)) continue;
		theyHaveMine_ = theyHaveMine_ || p.haveYours;
		if (theirs_) continue;
		if (partReceived_.empty()) {
			partReceived_.assign(p.parts, false);
			received_.assign((size_t)p.parts * kEntriesPerPart, start_state::Entry{});
		}
		if (p.parts != partReceived_.size() || partReceived_[p.part]) continue;
		std::copy(p.entries.begin(), p.entries.end(), received_.begin() + (ptrdiff_t)((size_t)p.part * kEntriesPerPart));
		partReceived_[p.part] = true;
		// The last part says how many entries the list has.
		if (p.part + 1 == p.parts) received_.resize((size_t)p.part * kEntriesPerPart + p.entries.size());
		if (std::find(partReceived_.begin(), partReceived_.end(), false) == partReceived_.end()) theirs_ = received_;
	}
	if (theirs_ && theyHaveMine_) {
		// Once more, so the other copy hears that this one has its list.
		SendAll();
		finished_ = true;
		return true;
	}
	if (nowMs - begunMs_ >= config_.timeLimitMs) {
		finished_ = true;
		return true;
	}
	if (nowMs - sentMs_ >= config_.resendMs) {
		SendAll();
		sentMs_ = nowMs;
	}
	return false;
}

} // namespace sfve
