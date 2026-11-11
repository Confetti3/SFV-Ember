#include "core/ReplayFile.hxx"

#include <xxhash.h>

#include <nlohmann/json.hpp>

#include <string.h>

#include <limits>

namespace sfve::replay {

namespace {

using json = nlohmann::json;

constexpr size_t kMagicBytes = sizeof(kMagic);
constexpr size_t kRunBytes = 13;       // u32 frames, u32 P1, u32 P2, u8 flags
constexpr size_t kCheckpointBytes = 12; // i32 frame, u64 hash
constexpr size_t kTrailerBytes = 8;
constexpr int kCheckpointInterval = 60; // RollbackSession::kCheckpointInterval

void PutU32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i)));
}
void PutU64(std::vector<uint8_t>& out, uint64_t v)
{
	for (int i = 0; i < 8; i++) out.push_back((uint8_t)(v >> (8 * i)));
}

// Reads forward through a bounded buffer; any read past its end fails.
class Reader {
public:
	Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
	bool Bytes(size_t n, const uint8_t** out)
	{
		if (n > size_ - at_) return false;
		*out = data_ + at_;
		at_ += n;
		return true;
	}
	bool U32(uint32_t* out)
	{
		const uint8_t* p = nullptr;
		if (!Bytes(4, &p)) return false;
		*out = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
		return true;
	}
	bool U64(uint64_t* out)
	{
		uint32_t lo = 0, hi = 0;
		if (!U32(&lo) || !U32(&hi)) return false;
		*out = (uint64_t)hi << 32 | lo;
		return true;
	}
	bool U8(uint8_t* out)
	{
		const uint8_t* p = nullptr;
		if (!Bytes(1, &p)) return false;
		*out = *p;
		return true;
	}
	size_t Left() const { return size_ - at_; }

private:
	const uint8_t* data_;
	size_t size_;
	size_t at_ = 0;
};

json PlayerJson(const Player& p)
{
	json j = {{"name", p.name}, {"fighter", p.fighter}, {"costume", p.costume}, {"colour", p.colour}, {"code", p.code},
		{"launchCostume", p.launchCostume}, {"launchColour", p.launchColour}, {"vSkill", p.vSkill}, {"vTrigger", p.vTrigger},
		{"loadCode", p.loadCode}};
	// Only a Tiny side writes it, so every other header is what it was.
	if (p.size != 0) j["size"] = p.size;
	return j;
}

json HeaderJson(const Header& h)
{
	json games = json::array();
	for (const Game& g : h.games) {
		games.push_back({{"decided", g.decided}, {"winner", g.winner}, {"wins", {g.wins[0], g.wins[1]}}, {"rounds", g.rounds},
			{"bestOf", g.bestOf}, {"endFrame", g.endFrame}});
	}
	return {{"product", h.product}, {"build", h.build}, {"startedAt", h.startedAt}, {"slot", h.slot},
		{"players", {PlayerJson(h.players[0]), PlayerJson(h.players[1])}}, {"stage", h.stage}, {"stageCode", h.stageCode},
		{"rounds", h.rounds}, {"roundSeconds", h.roundSeconds}, {"timeFrames", h.timeFrames}, {"seed", h.seed},
		{"inputDelay", h.inputDelay}, {"games", games}, {"complete", h.complete}, {"fightFrames", h.fightFrames},
		{"fightStretches", h.fightStretches}};
}

// Each field must be there and of its type (nlohmann's get<> throws, which
// the caller turns into a refusal); integers must also fit their field.
template <class T>
T Field(const json& object, const char* name)
{
	const json& value = object.at(name);
	if constexpr (std::is_same_v<T, bool>) {
		if (!value.is_boolean()) throw std::runtime_error(std::string(name) + " is not a boolean");
		return value.get<bool>();
	} else if constexpr (std::is_same_v<T, std::string>) {
		if (!value.is_string()) throw std::runtime_error(std::string(name) + " is not text");
		return value.get<std::string>();
	} else {
		if (!value.is_number_integer()) throw std::runtime_error(std::string(name) + " is not a whole number");
		if (value.is_number_unsigned()) {
			const uint64_t v = value.get<uint64_t>();
			if (v > (uint64_t)std::numeric_limits<T>::max()) throw std::runtime_error(std::string(name) + " is out of range");
			return (T)v;
		}
		const int64_t v = value.get<int64_t>();
		if (v < (int64_t)std::numeric_limits<T>::min() || (std::is_signed_v<T> ? v > (int64_t)std::numeric_limits<T>::max() : false)) {
			throw std::runtime_error(std::string(name) + " is out of range");
		}
		return (T)v;
	}
}

// A field a header written by an earlier version lacks: `absent` then, and
// otherwise as Field reads it.
template <class T>
T FieldOr(const json& object, const char* name, T absent)
{
	return object.contains(name) ? Field<T>(object, name) : absent;
}

Player ReadPlayer(const json& j)
{
	Player p;
	p.name = Field<std::string>(j, "name");
	p.fighter = Field<int>(j, "fighter");
	p.costume = Field<int>(j, "costume");
	p.colour = Field<int>(j, "colour");
	p.code = Field<std::string>(j, "code");
	p.launchCostume = Field<int>(j, "launchCostume");
	p.launchColour = Field<int>(j, "launchColour");
	p.vSkill = FieldOr<int>(j, "vSkill", 0);
	p.vTrigger = FieldOr<int>(j, "vTrigger", 0);
	p.loadCode = FieldOr<std::int64_t>(j, "loadCode", -1);
	if (p.loadCode < -1 || p.loadCode > 0xFFFFFFFFll) p.loadCode = -1;
	p.size = FieldOr<int>(j, "size", 0);
	if (p.size != 0 && p.size != 1) p.size = 0;
	return p;
}

bool ReadHeader(const uint8_t* text, size_t size, Header* out, std::string* error)
{
	try {
		const json j = json::parse(text, text + size);
		if (!j.is_object()) throw std::runtime_error("the header is not an object");
		Header h;
		h.product = Field<std::string>(j, "product");
		h.build = Field<std::string>(j, "build");
		h.startedAt = Field<uint64_t>(j, "startedAt");
		h.slot = Field<int>(j, "slot");
		const json& players = j.at("players");
		if (!players.is_array() || players.size() != 2) throw std::runtime_error("players is not two players");
		h.players[0] = ReadPlayer(players[0]);
		h.players[1] = ReadPlayer(players[1]);
		h.stage = Field<int64_t>(j, "stage");
		h.stageCode = Field<std::string>(j, "stageCode");
		h.rounds = Field<int>(j, "rounds");
		h.roundSeconds = Field<int>(j, "roundSeconds");
		h.timeFrames = Field<int>(j, "timeFrames");
		h.seed = Field<uint32_t>(j, "seed");
		h.inputDelay = Field<int>(j, "inputDelay");
		const json& games = j.at("games");
		if (!games.is_array() || games.size() > 1000) throw std::runtime_error("games is not a list of games");
		for (const json& g : games) {
			Game game;
			game.decided = Field<bool>(g, "decided");
			game.winner = Field<int>(g, "winner");
			const json& wins = g.at("wins");
			if (!wins.is_array() || wins.size() != 2 || !wins[0].is_number_integer() || !wins[1].is_number_integer()) {
				throw std::runtime_error("a game's wins are not two numbers");
			}
			game.wins[0] = wins[0].get<int>();
			game.wins[1] = wins[1].get<int>();
			game.rounds = Field<int>(g, "rounds");
			game.bestOf = Field<int>(g, "bestOf");
			game.endFrame = Field<int>(g, "endFrame");
			if (game.winner < -1 || game.winner > 1 || game.wins[0] < 0 || game.wins[1] < 0 || game.rounds < 0) {
				throw std::runtime_error("a game's result is out of range");
			}
			h.games.push_back(game);
		}
		h.complete = Field<bool>(j, "complete");
		h.fightFrames = Field<uint32_t>(j, "fightFrames");
		h.fightStretches = Field<uint32_t>(j, "fightStretches");
		if (h.slot < 0) throw std::runtime_error("slot is out of range");
		*out = std::move(h);
		return true;
	} catch (const std::exception& e) {
		if (error) *error = std::string("the header does not read: ") + e.what();
		return false;
	}
}

// Checks the magic, version and trailing hash; on success `body` reads the
// header length onwards, short of the trailer.
bool Open(const uint8_t* data, size_t size, Reader* body, std::string* error)
{
	const auto refuse = [&](const char* why) {
		if (error) *error = why;
		return false;
	};
	if (!data || size < kMagicBytes + 4 + kTrailerBytes) return refuse("too short to be a replay");
	if (size > kMostFileBytes) return refuse("larger than any replay");
	if (memcmp(data, kMagic, kMagicBytes) != 0) return refuse("not an SFV Ember replay");
	Reader r(data + kMagicBytes, size - kMagicBytes - kTrailerBytes);
	uint32_t version = 0;
	if (!r.U32(&version)) return refuse("too short to be a replay");
	if (version != kVersion) return refuse("a replay of another version");
	const uint8_t* trailer = data + size - kTrailerBytes;
	uint64_t stored = 0;
	for (int i = 0; i < 8; i++) stored |= (uint64_t)trailer[i] << (8 * i);
	if (XXH64(data, size - kTrailerBytes, 0) != stored) return refuse("damaged (its hash does not match)");
	*body = r;
	return true;
}

bool ReadHeaderBlock(Reader& r, Header* out, std::string* error)
{
	uint32_t headerBytes = 0;
	const uint8_t* text = nullptr;
	if (!r.U32(&headerBytes) || headerBytes > kMostHeaderBytes || !r.Bytes(headerBytes, &text)) {
		if (error) *error = "its header is past the file";
		return false;
	}
	return ReadHeader(text, headerBytes, out, error);
}

} // namespace

void CountFights(const ReplayFrames& frames, uint32_t* fightFrames, uint32_t* stretches)
{
	uint32_t fight = 0, runs = 0;
	bool in = false;
	for (const RecordedFrame& f : frames) {
		if (f.Fight()) {
			fight++;
			if (!in) runs++;
			in = true;
		} else {
			in = false; // an ignored or held frame ends the stretch
		}
	}
	*fightFrames = fight;
	*stretches = runs;
}

std::vector<uint8_t> Encode(const Replay& replay)
{
	std::vector<uint8_t> out(kMagic, kMagic + kMagicBytes);
	PutU32(out, kVersion);
	const std::string header = HeaderJson(replay.header).dump();
	PutU32(out, (uint32_t)header.size());
	out.insert(out.end(), header.begin(), header.end());

	std::vector<std::pair<uint32_t, RecordedFrame>> runs;
	for (const RecordedFrame& f : replay.frames) {
		if (!runs.empty() && runs.back().second == f && runs.back().first < UINT32_MAX) {
			runs.back().first++;
		} else {
			runs.push_back({1u, f});
		}
	}
	PutU32(out, (uint32_t)runs.size());
	for (const auto& [count, f] : runs) {
		PutU32(out, count);
		PutU32(out, f.inputs.p[0]);
		PutU32(out, f.inputs.p[1]);
		out.push_back(f.flags);
	}
	PutU32(out, (uint32_t)replay.checkpoints.size());
	for (const auto& [frame, hash] : replay.checkpoints) {
		PutU32(out, (uint32_t)frame);
		PutU64(out, hash);
	}
	PutU64(out, XXH64(out.data(), out.size(), 0));
	return out;
}

bool Decode(const uint8_t* data, size_t size, Replay* out, std::string* error)
{
	const auto refuse = [&](const char* why) {
		if (error) *error = why;
		return false;
	};
	Reader r(nullptr, 0);
	if (!Open(data, size, &r, error)) return false;
	Replay replay;
	if (!ReadHeaderBlock(r, &replay.header, error)) return false;

	uint32_t runs = 0;
	if (!r.U32(&runs) || runs > r.Left() / kRunBytes) return refuse("its frames are past the file");
	for (uint32_t i = 0; i < runs; i++) {
		uint32_t count = 0;
		RecordedFrame f;
		if (!r.U32(&count) || !r.U32(&f.inputs.p[0]) || !r.U32(&f.inputs.p[1]) || !r.U8(&f.flags)) {
			return refuse("its frames are past the file");
		}
		if (count == 0) return refuse("a run of no frames");
		if ((f.flags & ~RecordedFrame::kKnown) != 0 || (f.Held() && f.Fight())) return refuse("a frame of an unknown kind");
		if (count > kMostFrames - replay.frames.size()) return refuse("more frames than any battle");
		replay.frames.insert(replay.frames.end(), count, f);
	}
	uint32_t checkpoints = 0;
	if (!r.U32(&checkpoints) || checkpoints > r.Left() / kCheckpointBytes) return refuse("its checkpoints are past the file");
	int previous = -1;
	for (uint32_t i = 0; i < checkpoints; i++) {
		uint32_t frame = 0;
		uint64_t hash = 0;
		if (!r.U32(&frame) || !r.U64(&hash)) return refuse("its checkpoints are past the file");
		// A checkpoint is a state the frames reach: frame N is the state after N frames.
		if ((int)frame <= previous || frame > replay.frames.size() || frame % kCheckpointInterval != 0) {
			return refuse("a checkpoint out of order or past the frames");
		}
		previous = (int)frame;
		replay.checkpoints.push_back({(int)frame, hash});
	}
	if (r.Left() != 0) return refuse("bytes after its checkpoints");
	*out = std::move(replay);
	return true;
}

bool DecodeHeader(const uint8_t* data, size_t size, Header* out, std::string* error)
{
	Reader r(nullptr, 0);
	return Open(data, size, &r, error) && ReadHeaderBlock(r, out, error);
}

} // namespace sfve::replay
