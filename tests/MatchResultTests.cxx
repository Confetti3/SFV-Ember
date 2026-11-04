#include <doctest/doctest.h>

#include "mod/MatchResult.hxx"
#include "mod/SfvOffsets.hxx"

#include <string.h>

#include <vector>

using namespace sfve::mod;
namespace bd = sfve::mod::sfv::battledata;

namespace {

// The battle's match data, as far as the result reaches.
struct MatchData {
	std::vector<uint8_t> bytes = std::vector<uint8_t>(bd::kResults + bd::kResultsSize);

	void Set(uint32_t offset, int32_t value) { memcpy(bytes.data() + offset, &value, sizeof(value)); }
	void Round(int index, int32_t cause, int32_t winner, int32_t finish)
	{
		const uint32_t at = bd::kRounds + bd::kRoundStride * index;
		Set(at + bd::kRoundCause, cause);
		Set(at + bd::kRoundWinner, winner);
		Set(at + bd::kRoundFinish, finish);
	}
};

} // namespace

TEST_CASE("a match won two rounds to none")
{
	MatchData m;
	m.Set(bd::kRoundsSetting, 3);
	m.Set(bd::kRoundCount, 2);
	m.Round(0, bd::kCauseKo, 0, 1); // KO
	m.Round(1, bd::kCauseKo, 0, 3); // Critical Art finish
	m.Set(bd::kDecided, 1);
	m.Set(bd::kMatchWinner, 0);
	const MatchResult r = ReadMatchResult(m.bytes.data());
	CHECK(r.decided);
	CHECK(r.winner == 0);
	CHECK(r.bestOf == 3);
	REQUIRE(r.rounds == 2);
	CHECK(r.round[0].finish == Finish::Ko);
	CHECK(r.round[1].finish == Finish::CriticalArt);
	CHECK(r.wins[0] == 2);
	CHECK(r.wins[1] == 0);
	CHECK(DescribeMatchResult(r) ==
		R"({"decided":true,"winner":0,"bestOf":3,"wins":[2,0],"rounds":[{"winner":0,"finish":"ko"},{"winner":0,"finish":"critical art"}]})");
}

TEST_CASE("a round nobody wins counts for both sides, as the game counts it")
{
	MatchData m;
	m.Set(bd::kRoundsSetting, 3);
	m.Set(bd::kRoundCount, 3);
	m.Round(0, bd::kCauseKo, 1, 6); // perfect
	m.Round(1, bd::kCauseKo, -1, 2); // double KO
	m.Round(2, 1, -1, 7);            // time over, equal health
	m.Set(bd::kDecided, 1);
	m.Set(bd::kMatchWinner, 1);
	const MatchResult r = ReadMatchResult(m.bytes.data());
	CHECK(r.winner == 1);
	CHECK(r.round[0].finish == Finish::Perfect);
	CHECK(r.round[1].finish == Finish::DoubleKo);
	CHECK(r.round[1].winner == -1);
	CHECK(r.round[2].finish == Finish::TimeOver);
	CHECK(r.wins[0] == 2);
	CHECK(r.wins[1] == 3);
}

TEST_CASE("finishes written by other rules are not KOs; odd values are clamped")
{
	MatchData m;
	m.Set(bd::kRoundCount, 99); // more than the seven records kept
	m.Round(0, 64, 0, 1);       // a forfeit writes finish 1 too
	m.Round(1, bd::kCauseKo, 2, 1);
	m.Set(bd::kMatchWinner, 3); // sides 2 and 3 never play a versus match
	const MatchResult r = ReadMatchResult(m.bytes.data());
	CHECK(r.rounds == MatchResult::kMaxRounds);
	CHECK(r.round[0].finish == Finish::Other);
	CHECK(r.round[1].winner == -1);
	CHECK(r.winner == -1);
	CHECK_FALSE(r.decided);
	CHECK(ReadMatchResult(nullptr).rounds == 0);
}
