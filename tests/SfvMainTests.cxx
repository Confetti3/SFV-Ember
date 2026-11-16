// A player's main on SFV Ember (EMBER_LIBRARY.md section 62): none until the
// player chooses one, saved and read back by its index in SFV's own roster
// (the catalog's 45 fighters, never SF4's 44), and a main nobody chose never
// reaches the room as a fighter, so the opponent's Ready stands through the
// player's first pick.
#include "CatalogFixture.hxx"

#include "common/ProductIdentity.hxx"
#include "netplay/PlayerPreferencesJson.hxx"
#include "session/RoomModel.hxx"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::catalog_fixture;

namespace {

// SFV's roster at its size: 45 fighters, the last one past SF4's 44.
void FillFullRoster()
{
	std::vector<std::string> codes;
	for (int i = 0; i < 45; i++) codes.push_back("F" + std::to_string(100 + i));
	REQUIRE(adapter::FillCatalog(CodesOnly(codes, {"S01"})));
	REQUIRE(ember::Catalog().fighterCount == 45);
}

sf4e::room::ConnectionRef Peer(int index)
{
	return sf4e::room::ConnectionRef{"host", std::to_string(index)};
}

sf4e::room::Action TableAction(const sf4e::room::RoomAuthority& authority, sf4e::room::MemberId member, sf4e::room::ActionKind kind)
{
	sf4e::room::Action action;
	action.kind = kind;
	action.roomEpoch = authority.SnapshotView().roomEpoch;
	action.revision = authority.SnapshotView().revision;
	action.table = 0;
	action.tableRevision = authority.SnapshotView().tables[0].revision;
	action.actionId = member * 1000 + authority.SnapshotView().revision + 1;
	return action;
}

// P1 has readied at table 0; whether P2's first pick takes P1's Ready back,
// with P2's profile main as SFV Ember sends it.
bool FirstPickWithdraws(int p2Main, int p2Pick)
{
	using namespace sf4e::room;
	RoomAuthority authority("Main", 8, 1);
	REQUIRE(authority.Join("Host", Peer(0), true).accepted);
	const auto p1Joined = authority.Join("Player1", Peer(1), false);
	REQUIRE(p1Joined.accepted);
	const MemberId p1 = p1Joined.snapshot.members.back().id;
	MemberProfile profile;
	profile.mainFighter = p2Main;
	const auto p2Joined = authority.Join("Player2", Peer(2), false, profile);
	REQUIRE(p2Joined.accepted);
	const MemberId p2 = p2Joined.snapshot.members.back().id;
	for (const MemberId member : {p1, p2}) REQUIRE(authority.Apply(member, TableAction(authority, member, ActionKind::Queue)).accepted);
	REQUIRE(authority.SetMemberChara(p1, {0}));
	Action ready = TableAction(authority, p1, ActionKind::Ready);
	ready.inputDelay = 2;
	REQUIRE(authority.Apply(p1, ready).accepted);
	bool withdrew = false;
	REQUIRE(authority.SetMemberChara(p2, {p2Pick}, &withdrew));
	CHECK(authority.SnapshotView().tables[0].ready[0] == !withdrew);
	return withdrew;
}

} // namespace

TEST_CASE("an SFV player has no main until they choose one")
{
	FillFullRoster();
	CHECK(ember::Product().branding.mainStartsUnset);
	CHECK(ember::UnsetMain() == -1);
	// Nothing saved, or a saved main that is no fighter of this roster, is none.
	CHECK(sf4e::netplay::ReadMainFighter(nlohmann::json::object()) == -1);
	CHECK(sf4e::netplay::ReadMainFighter({{"mainFighter", 45}}) == -1);
	CHECK(sf4e::netplay::ReadMainFighter({{"mainFighter", -1}}) == -1);
	// Settings with no main yet are valid settings, and save as none.
	sf4e::netplay::PlayerPreferences preferences;
	preferences.mainFighter = ember::UnsetMain();
	CHECK(preferences.Valid());
	CHECK(sf4e::netplay::ReadMainFighter(sf4e::netplay::PlayerPreferencesJson(preferences)) == -1);
}

TEST_CASE("a main nobody chose stays none: a fresh start, an unread save and an old build's default")
{
	FillFullRoster();
	// What a start that could not read the settings has, and every default copy of them.
	CHECK(sf4e::netplay::PlayerPreferences{}.mainFighter == -1);
	// The builds before beta.10 saved Ryu (0) as everyone's main, chosen or not, and no "mainChosen".
	CHECK(sf4e::netplay::ReadMainFighter({{"mainFighter", 0}}) == -1);
	// A main chosen since is marked, and any other fighter was always a choice.
	CHECK(sf4e::netplay::ReadMainFighter({{"mainFighter", 0}, {"mainChosen", true}}) == 0);
	CHECK(sf4e::netplay::ReadMainFighter({{"mainFighter", 42}}) == 42);
	sf4e::netplay::PlayerPreferences chosen;
	chosen.mainFighter = 0;
	CHECK(sf4e::netplay::ReadMainFighter(sf4e::netplay::PlayerPreferencesJson(chosen)) == 0);
}

TEST_CASE("SFV's pictures: the packaged cutouts' shape and a face for a portrait")
{
	// The card's frames take these (PlayerCard.cxx): the package's cutouts are 256x384 with the figure fitted in 224x352
	// (docs/design/beta8/pack.md), and the portrait is the head's square from the select grid (SfvFaceCrop).
	const ember::PictureShapes& pictures = ember::ProductPictures();
	CHECK(pictures.figureWidth == 224.f);
	CHECK(pictures.figureHeight == 352.f);
	CHECK(pictures.portraitIsFace);
}

TEST_CASE("a main past SF4's 44 fighters is saved and read back")
{
	FillFullRoster();
	for (const int main : {0, 43, 44}) {
		sf4e::netplay::PlayerPreferences preferences;
		preferences.mainFighter = main;
		CHECK(preferences.Valid());
		const nlohmann::json saved = sf4e::netplay::PlayerPreferencesJson(preferences);
		CHECK(saved.at("mainFighter") == main);
		CHECK(sf4e::netplay::ReadMainFighter(saved) == main);
	}
}

TEST_CASE("a first pick over no main keeps the opponent's Ready")
{
	FillFullRoster();
	// SFV Ember sends -1 until its player chooses a main: their first pick,
	// fighter 44 here, is no change of the matchup the opponent readied against.
	CHECK_FALSE(FirstPickWithdraws(-1, 44));
	// A chosen main is the matchup until the first pick, as on SF4 Ember: a
	// first pick of another fighter takes the opponent's Ready back, one of
	// the main does not.
	CHECK(FirstPickWithdraws(44, 3));
	CHECK_FALSE(FirstPickWithdraws(44, 44));
}
