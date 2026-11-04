#pragma once

// Who won an SFV versus match, read from the battle's match data at the
// match end (flow state MatchEnd): the game's own judgement, as its results
// screen and round icons show it (sfv::battledata). It lies in the state the
// mod restores on rollback, so both linked copies read the same result.

#include <stdint.h>

#include <string>

namespace sfve::mod {

// How a round ended, in the order of the game's finish icons.
enum class Finish {
	Undecided,
	Ko,
	DoubleKo,
	CriticalArt,
	Ex,
	Chip,
	Perfect,
	TimeOver,
	Other, // ended by a rule other than a KO or the clock
};

struct RoundResult {
	int winner = -1; // side 0 (P1) or 1 (P2); -1 none (a double KO or a draw), which counts for both
	Finish finish = Finish::Undecided;
};

struct MatchResult {
	static constexpr int kMaxRounds = 7;
	bool decided = false;
	int winner = -1;  // side 0 or 1, -1 a draw
	int bestOf = 0;   // the match's rounds setting
	int rounds = 0;   // rounds judged
	RoundResult round[kMaxRounds];
	int wins[2] = {}; // rounds won by each side, as the game counts them
};

// Reads the result from the battle's match data (SfvAdapter::BattleData).
MatchResult ReadMatchResult(const uint8_t* battleData);

const char* FinishName(Finish finish);
std::string DescribeMatchResult(const MatchResult& result); // JSON

} // namespace sfve::mod
