#include "mod/MatchResult.hxx"

#include "mod/SfvOffsets.hxx"

#include <stdio.h>
#include <string.h>

namespace sfve::mod {

namespace {

namespace bd = sfv::battledata;

int32_t Int(const uint8_t* at, uint32_t offset)
{
	int32_t value;
	memcpy(&value, at + offset, sizeof(value));
	return value;
}

Finish FinishOf(int32_t cause, int32_t finish)
{
	switch (finish) {
	case 0: return Finish::Undecided;
	case 2: return Finish::DoubleKo;
	case 7: return Finish::TimeOver;
	default: break;
	}
	// Finishes 1 and 3-6 are a KO only when the judge's cause was one; the
	// other rules also write finish 1.
	if (cause != bd::kCauseKo) return Finish::Other;
	switch (finish) {
	case 1: return Finish::Ko;
	case 3: return Finish::CriticalArt;
	case 4: return Finish::Ex;
	case 5: return Finish::Chip;
	case 6: return Finish::Perfect;
	default: return Finish::Other;
	}
}

int Side(int32_t side)
{
	return side == 0 || side == 1 ? side : -1;
}

} // namespace

MatchResult ReadMatchResult(const uint8_t* battleData)
{
	MatchResult r;
	if (!battleData) return r;
	r.decided = Int(battleData, bd::kDecided) != 0;
	r.winner = Side(Int(battleData, bd::kMatchWinner));
	r.bestOf = Int(battleData, bd::kRoundsSetting);
	const int32_t judged = Int(battleData, bd::kRoundCount);
	r.rounds = judged < 0 ? 0 : judged > MatchResult::kMaxRounds ? MatchResult::kMaxRounds : judged;
	for (int i = 0; i < r.rounds; i++) {
		const uint8_t* record = battleData + bd::kRounds + bd::kRoundStride * i;
		const int32_t finish = Int(record, bd::kRoundFinish);
		RoundResult& round = r.round[i];
		round.winner = Side(Int(record, bd::kRoundWinner));
		round.finish = FinishOf(Int(record, bd::kRoundCause), finish);
		if (finish == 0) continue;
		// The game's count (rva 0x2F91C0): a round won by nobody counts for both.
		if (round.winner != 1) r.wins[0]++;
		if (round.winner != 0) r.wins[1]++;
	}
	return r;
}

const char* FinishName(Finish finish)
{
	switch (finish) {
	case Finish::Undecided: return "undecided";
	case Finish::Ko: return "ko";
	case Finish::DoubleKo: return "double ko";
	case Finish::CriticalArt: return "critical art";
	case Finish::Ex: return "ex";
	case Finish::Chip: return "chip";
	case Finish::Perfect: return "perfect";
	case Finish::TimeOver: return "time over";
	case Finish::Other: return "other";
	}
	return "?";
}

std::string DescribeMatchResult(const MatchResult& r)
{
	char text[96];
	snprintf(text, sizeof(text), "{\"decided\":%s,\"winner\":%d,\"bestOf\":%d,\"wins\":[%d,%d],\"rounds\":[",
		r.decided ? "true" : "false", r.winner, r.bestOf, r.wins[0], r.wins[1]);
	std::string out = text;
	for (int i = 0; i < r.rounds; i++) {
		snprintf(text, sizeof(text), "%s{\"winner\":%d,\"finish\":\"%s\"}", i ? "," : "", r.round[i].winner,
			FinishName(r.round[i].finish));
		out += text;
	}
	return out + "]}";
}

} // namespace sfve::mod
