// The menu backdrop's rules (SfvBackdropRules), checked without the game:
// the art set this checkout ships parses, is the one embedded, and lists only
// object paths of the right form; the cycle holds, cross-fades, shuffles with
// no picture twice in a row, skips a picture that does not load, never keeps
// more than two alive, steps on demand and lets everything go when turned
// off; each page gets its scrim; and a picture covers any screen at its own
// shape.

#include <doctest/doctest.h>

#include "ember/SfvBackdropRules.hxx"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace sfve::backdrop;

namespace {

std::string ReadFile(const char* path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

ArtSet Parsed(const std::string& json)
{
	ArtSet set;
	std::string error;
	INFO(error);
	REQUIRE(ParseArtSet(json, set, error));
	CHECK(error.empty());
	return set;
}

// The parse's refusal of `json`, which leaves the set as it was.
std::string Refusal(const std::string& json)
{
	ArtSet set;
	set.families.push_back({"kept", true, {"A/B"}});
	std::string error;
	INFO(json);
	CHECK_FALSE(ParseArtSet(json, set, error));
	REQUIRE(set.families.size() == 1);
	CHECK(set.families[0].name == "kept");
	CHECK_FALSE(error.empty());
	return error;
}

std::string OneFamily(const std::string& paths, const std::string& extra = "")
{
	return R"({"version": 1, "families": [{"name": "f", "use": true)" + extra + R"(, "paths": [)" + paths + "]}]}";
}

constexpr double kFrame = 1.0 / 60;

// The loader and the drawer around a cycle, as SfvBackdrop runs them: each
// frame, a wanted picture loads at once (or fails where `fails` says), the
// clock moves a frame, and what the cycle lets go is let go. Every frame
// checks that no more than two pictures are alive, on the cycle's count and
// on the loader's, and that nothing is let go that was not loaded.
struct Run {
	Cycle cycle;
	std::set<int> alive;
	std::vector<int> shown; // each picture as it became the one shown
	std::function<bool(int)> fails = [](int) { return false; };
	std::vector<int> asked;
	double time = 0;

	explicit Run(int entries, std::uint32_t seed = 7, Timing timing = {}) : cycle(entries, seed, timing) {}

	void Frame(double seconds = kFrame)
	{
		if (const std::optional<int> wanted = cycle.Wanted()) {
			asked.push_back(*wanted);
			cycle.Started(*wanted);
			CHECK_FALSE(cycle.Wanted().has_value()); // one load at a time
			const bool ok = !fails(*wanted);
			if (cycle.Loaded(*wanted, ok)) alive.insert(*wanted);
			else CHECK_FALSE(ok);
		}
		const int before = cycle.Current();
		cycle.Advance(seconds);
		time += (std::min)(seconds, kMostStep);
		for (int entry : cycle.TakeReleases()) CHECK(alive.erase(entry) == 1);
		CHECK(cycle.Alive() <= 2);
		CHECK(alive.size() <= 2);
		if (cycle.Current() != before && cycle.Current() >= 0) shown.push_back(cycle.Current());
	}
	void For(double seconds)
	{
		const double until = time + seconds;
		while (time < until) Frame();
	}
	// Frames until `done`, at most `limit` seconds; the time it took.
	double Until(const std::function<bool()>& done, double limit = 120)
	{
		const double start = time;
		while (!done() && time - start < limit) Frame();
		REQUIRE(done());
		return time - start;
	}
};

} // namespace

TEST_CASE("the art set this checkout ships parses, and is the one embedded")
{
	const ArtSet file = Parsed(ReadFile(SFVE_MENU_ART_PATH));
	const ArtSet& embedded = EmbeddedArtSet();
	REQUIRE(embedded.families.size() == file.families.size());
	for (size_t i = 0; i < file.families.size(); i++) {
		CHECK(embedded.families[i].name == file.families[i].name);
		CHECK(embedded.families[i].use == file.families[i].use);
		CHECK(embedded.families[i].paths == file.families[i].paths);
	}
}

TEST_CASE("the art set's families: the gallery's special and ending pictures on, the others off until seen")
{
	const ArtSet& set = EmbeddedArtSet();
	std::map<std::string, const Family*> byName;
	for (const Family& family : set.families) byName[family.name] = &family;
	REQUIRE(byName.size() == 4);
	// Each family's pictures are where its name says, and it is on or off.
	const struct {
		const char* name;
		const char* folder;
		bool use;
	} expected[] = {
		{"gallery-special", "GameMode/Gallery/Illust/special/", true},
		{"gallery-ending", "GameMode/Gallery/Illust/ending/", true},
		{"gallery-mode03", "GameMode/MODE03/Image/Gallery/Illust/", false},
		{"story", "Chara/", false},
	};
	size_t used = 0;
	for (const auto& want : expected) {
		INFO(want.name);
		REQUIRE(byName.count(want.name) == 1);
		const Family& family = *byName[want.name];
		CHECK(family.use == want.use);
		CHECK_FALSE(family.paths.empty());
		for (const std::string& path : family.paths) {
			INFO(path);
			CHECK(path.rfind(want.folder, 0) == 0);
			if (family.name == "story") CHECK(path.find("/IllustDemo/Image/") != std::string::npos);
		}
		if (family.use) used += family.paths.size();
	}
	CHECK(UsedPaths(set).size() == used);
	CHECK(used > 100);
	CHECK(AllPaths(set).size() > used);
}

TEST_CASE("every listed path is an object path under /Game/, once")
{
	std::set<std::string> seen;
	for (const ListedPath& listed : AllPaths(EmbeddedArtSet())) {
		INFO(listed.path);
		CHECK(ArtPathValid(listed.path));
		CHECK(seen.insert(listed.path).second);
		const std::wstring object = ObjectPathOf(listed.path);
		const std::string name = listed.path.substr(listed.path.rfind('/') + 1);
		const std::wstring expected = L"/Game/" + std::wstring(listed.path.begin(), listed.path.end()) + L"." +
			std::wstring(name.begin(), name.end());
		CHECK(object == expected);
	}
	CHECK(ObjectPathOf("GameMode/Gallery/Illust/special/end_sf1_spl_001") ==
		L"/Game/GameMode/Gallery/Illust/special/end_sf1_spl_001.end_sf1_spl_001");
}

TEST_CASE("a path is names of letters, digits and underscores joined by slashes")
{
	CHECK(ArtPathValid("Chara/RYU/IllustDemo/Image/RYU_001_001"));
	CHECK(ArtPathValid("A/B"));
	CHECK_FALSE(ArtPathValid(""));
	CHECK_FALSE(ArtPathValid("Single"));
	CHECK_FALSE(ArtPathValid("/Game/Chara/RYU/x"));
	CHECK_FALSE(ArtPathValid("Chara/RYU/x.x"));
	CHECK_FALSE(ArtPathValid("Chara/RYU/x.uasset"));
	CHECK_FALSE(ArtPathValid("Chara/../x"));
	CHECK_FALSE(ArtPathValid("Chara//x"));
	CHECK_FALSE(ArtPathValid("Chara/x/"));
	CHECK_FALSE(ArtPathValid("Chara\\x"));
	CHECK_FALSE(ArtPathValid("Chara/x y"));
	CHECK_FALSE(ArtPathValid("A/" + std::string(200, 'x')));
	CHECK(ObjectPathOf("Single").empty());
	CHECK(ObjectPathOf("A/b.c").empty());
}

TEST_CASE("an art set that breaks a rule does not parse")
{
	CHECK(Parsed(OneFamily(R"("A/B", "C/D")", R"(, "note": "two")")).families[0].paths.size() == 2);
	Refusal("not json");
	Refusal(R"({"version": 2, "families": []})");
	Refusal(R"({"version": 1})");
	Refusal(R"({"version": 1, "families": [], "extra": 1})");
	Refusal(R"({"version": 1, "families": [{"name": "f", "use": true, "path": ["A/B"]}]})");
	Refusal(R"({"version": 1, "families": [{"name": "", "use": true, "paths": []}]})");
	Refusal(R"({"version": 1, "families": [{"name": "f", "use": "yes", "paths": []}]})");
	Refusal(R"({"version": 1, "families": [{"name": "f", "use": true, "paths": []}, {"name": "f", "use": false, "paths": []}]})");
	Refusal(OneFamily(R"("A/B", "A/B")"));
	Refusal(OneFamily(R"("/Game/A/B")"));
	Refusal(OneFamily(R"("A/B.B")"));
	Refusal(OneFamily(R"(3)"));
	Refusal(OneFamily(R"("A/B")", R"(, "note": 1)"));
	// A path listed in two families is listed twice.
	Refusal(R"({"version": 1, "families": [{"name": "f", "use": true, "paths": ["A/B"]}, {"name": "g", "use": false, "paths": ["A/B"]}]})");
}

TEST_CASE("only the families in use are in the cycle, in the file's order")
{
	const ArtSet set = Parsed(R"({"version": 1, "families": [
		{"name": "a", "use": true, "paths": ["A/One", "A/Two"]},
		{"name": "b", "use": false, "paths": ["B/Three"]},
		{"name": "c", "use": true, "paths": ["C/Four"]}]})");
	CHECK(UsedPaths(set) == std::vector<std::string>{"A/One", "A/Two", "C/Four"});
	const std::vector<ListedPath> all = AllPaths(set);
	REQUIRE(all.size() == 4);
	CHECK(all[2].family == "b");
	CHECK(all[2].path == "B/Three");
	CHECK_FALSE(all[2].used);
	CHECK(all[3].used);
}

TEST_CASE("a picture fades in, holds, then cross-fades into the next, and the one faded out is let go")
{
	const Timing timing;
	Run run(10, 3, timing);
	CHECK(run.cycle.Shown().Empty());
	// The first picture fades in from the plain ground.
	run.Until([&] { return run.cycle.Fading(); }, 1);
	const Cycle::Frame first = run.cycle.Shown();
	CHECK(first.under.entry < 0);
	CHECK(first.over.entry >= 0);
	CHECK(first.over.alpha < 0.05f);
	const double fadeIn = run.Until([&] { return !run.cycle.Fading(); });
	CHECK(fadeIn == doctest::Approx(timing.fade).epsilon(0.05));
	REQUIRE(run.shown.size() == 1);
	const int held = run.cycle.Current();
	CHECK(run.cycle.Shown().under.entry == held);
	CHECK(run.cycle.Shown().under.alpha == 1.f);
	CHECK(run.cycle.Shown().over.entry < 0);
	// Held, the next one loaded meanwhile, then the cross-fade.
	const double hold = run.Until([&] { return run.cycle.Fading(); });
	CHECK(hold == doctest::Approx(timing.hold).epsilon(0.01));
	CHECK(run.cycle.Shown().under.entry == held);
	CHECK(run.cycle.Shown().over.entry == run.cycle.Next());
	CHECK(run.alive.size() == 2);
	run.For(timing.fade / 2);
	const float middle = run.cycle.Shown().over.alpha;
	CHECK(middle > 0.3f);
	CHECK(middle < 0.7f);
	run.Until([&] { return !run.cycle.Fading(); });
	REQUIRE(run.shown.size() == 2);
	CHECK(run.shown[1] != held);
	CHECK(run.alive.count(held) == 0);
	CHECK(run.alive.size() == 1);
}

TEST_CASE("the next picture is loaded only once a fade has settled, and one at a time")
{
	Run run(10, 5);
	run.Until([&] { return run.shown.size() == 1; });
	CHECK(run.asked.size() == 1);
	// No load during the fade or its settling time.
	run.For(Timing{}.settle - 0.1);
	CHECK(run.asked.size() == 1);
	run.For(0.2);
	CHECK(run.asked.size() == 2);
	CHECK(run.cycle.NextReady());
	run.For(10);
	CHECK(run.asked.size() == 2);
}

TEST_CASE("every picture shows once a round, in a shuffled order, and never twice in a row")
{
	const Timing quick{2, 0.2, 0.1, 0.1};
	for (std::uint32_t seed : {1u, 2u, 99u}) {
		Run run(6, seed, quick);
		run.Until([&] { return run.shown.size() >= 6 * 5; }, 600);
		for (size_t i = 1; i < run.shown.size(); i++) CHECK(run.shown[i] != run.shown[i - 1]);
		// The first round has every picture once.
		const std::set<int> round(run.shown.begin(), run.shown.begin() + 6);
		CHECK(round.size() == 6);
		for (int entry : run.shown) {
			CHECK(entry >= 0);
			CHECK(entry < 6);
		}
	}
	// Two seeds give two orders.
	Run a(20, 11, quick), b(20, 12, quick);
	a.Until([&] { return a.shown.size() >= 10; }, 600);
	b.Until([&] { return b.shown.size() >= 10; }, 600);
	CHECK(std::vector<int>(a.shown.begin(), a.shown.begin() + 10) != std::vector<int>(b.shown.begin(), b.shown.begin() + 10));
}

TEST_CASE("a picture that does not load is skipped for the session, after a pause")
{
	const Timing quick{2, 0.2, 1.0, 0.1};
	Run run(5, 21, quick);
	run.fails = [](int entry) { return entry == 1 || entry == 3; };
	run.Until([&] { return run.shown.size() >= 12; }, 600);
	for (int entry : run.shown) {
		CHECK(entry != 1);
		CHECK(entry != 3);
	}
	CHECK(std::count(run.asked.begin(), run.asked.end(), 1) == 1);
	CHECK(std::count(run.asked.begin(), run.asked.end(), 3) == 1);
	CHECK(run.cycle.Failed() == 2);
	CHECK_FALSE(run.cycle.Exhausted());
	// The pause after a failure: the next try waits the retry time.
	Cycle cycle(3, 4, quick);
	const int first = *cycle.Wanted();
	cycle.Started(first);
	CHECK_FALSE(cycle.Loaded(first, false));
	CHECK_FALSE(cycle.Wanted().has_value());
	cycle.Advance(0.25);
	cycle.Advance(0.25);
	cycle.Advance(0.25);
	CHECK_FALSE(cycle.Wanted().has_value());
	cycle.Advance(0.25);
	REQUIRE(cycle.Wanted().has_value());
	CHECK(*cycle.Wanted() != first);
}

TEST_CASE("when no picture loads, nothing is ever shown and the cycle stops asking")
{
	Run run(4, 8, Timing{2, 0.2, 0.1, 0.1});
	run.fails = [](int) { return true; };
	run.For(30);
	CHECK(run.cycle.Exhausted());
	CHECK(run.cycle.Shown().Empty());
	CHECK(run.asked.size() == 4);
	CHECK_FALSE(run.cycle.Wanted().has_value());
	CHECK(run.cycle.Alive() == 0);
	// An empty set is exhausted from the start.
	Cycle none(0, 1);
	CHECK(none.Exhausted());
	CHECK_FALSE(none.Wanted().has_value());
	CHECK(none.Shown().Empty());
	Cycle unset;
	CHECK(unset.Exhausted());
}

TEST_CASE("a single picture fades in once and stays")
{
	Run run(1, 2, Timing{2, 0.2, 0.1, 0.1});
	run.For(60);
	CHECK(run.shown == std::vector<int>{0});
	CHECK(run.asked == std::vector<int>{0});
	CHECK_FALSE(run.cycle.Fading());
	CHECK(run.cycle.Alive() == 1);
}

TEST_CASE("never more than two pictures alive, whatever loads and whatever is stepped")
{
	Run run(30, 77, Timing{1, 0.3, 0.2, 0.0});
	int calls = 0;
	run.fails = [&calls](int) { return ++calls % 4 == 0; };
	for (int i = 0; i < 3000; i++) {
		if (i % 37 == 0) run.cycle.Step();
		if (i % 500 == 250) run.cycle.Reset();
		run.Frame(i % 13 == 0 ? 0.5 : kFrame);
	}
	CHECK(run.shown.size() > 10);
}

TEST_CASE("a step fades to the next picture as soon as it has loaded")
{
	Run run(10, 31);
	run.Until([&] { return run.shown.size() == 1; });
	// Still settling: the step lets the next load at once, then fades to it.
	run.cycle.Step();
	const double took = run.Until([&] { return run.shown.size() == 2; });
	CHECK(took < Timing{}.fade + 0.2);
	// Stepped while held, with the next already loaded.
	run.For(5);
	CHECK(run.cycle.NextReady());
	run.cycle.Step();
	run.Frame();
	CHECK(run.cycle.Fading());
}

TEST_CASE("turned off, everything alive is let go, and a picture still loading is refused when it arrives")
{
	Cycle cycle(5, 13, Timing{2, 0.2, 0.1, 0.1});
	// One shown, the next loading.
	int shownEntry = *cycle.Wanted();
	cycle.Started(shownEntry);
	CHECK(cycle.Loaded(shownEntry, true));
	for (int i = 0; i < 20; i++) cycle.Advance(0.05);
	REQUIRE(cycle.Current() == shownEntry);
	const int loading = *cycle.Wanted();
	cycle.Started(loading);
	CHECK(cycle.Alive() == 2);
	cycle.Reset();
	CHECK(cycle.TakeReleases() == std::vector<int>{shownEntry});
	CHECK(cycle.Shown().Empty());
	// Nothing new is loaded while the refused one is on its way.
	CHECK_FALSE(cycle.Wanted().has_value());
	CHECK(cycle.Alive() == 1);
	CHECK_FALSE(cycle.Loaded(loading, true));
	CHECK(cycle.Alive() == 0);
	// On again, the cycle goes on with the next.
	REQUIRE(cycle.Wanted().has_value());
	// Turned off mid-fade: both let go.
	Run run(5, 17, Timing{2, 0.4, 0.1, 0.1});
	run.Until([&] { return run.shown.size() == 1; });
	run.Until([&] { return run.cycle.Fading(); });
	run.cycle.Reset();
	const std::vector<int> released = run.cycle.TakeReleases();
	CHECK(released.size() == 2);
	CHECK(run.cycle.Shown().Empty());
	CHECK(run.cycle.Alive() == 0);
}

TEST_CASE("the clock moves at most a quarter second a frame, so a long frame never skips a fade")
{
	Run run(4, 41);
	run.Until([&] { return run.cycle.Fading(); }, 1);
	run.Frame(10);
	CHECK(run.cycle.Fading());
	CHECK(run.cycle.Shown().over.alpha < 0.5f);
	run.Frame(-3);
	CHECK(run.cycle.Fading());
}

TEST_CASE("the fade's curve eases in and out")
{
	CHECK(FadeCurve(-1) == 0.f);
	CHECK(FadeCurve(0) == 0.f);
	CHECK(FadeCurve(0.5f) == doctest::Approx(0.5f));
	CHECK(FadeCurve(1) == 1.f);
	CHECK(FadeCurve(2) == 1.f);
	float last = 0;
	for (int i = 1; i <= 20; i++) {
		const float value = FadeCurve(i / 20.f);
		CHECK(value >= last);
		last = value;
	}
	CHECK(FadeCurve(0.1f) < 0.1f);
}

TEST_CASE("a column page's scrim is light and left-heavy, a dense page's heavier and even")
{
	for (const char* screen : {"home", "online", "training"}) CHECK(PageKindOf(screen) == PageKind::Column);
	for (const char* screen : {"selection", "room", "room-table", "tab-interface", "tab-interface/look", "profile", "replays",
			 "notifications", "public-rooms", "create", "post-match", "", "unknown"}) {
		INFO(screen);
		CHECK(PageKindOf(screen) == PageKind::Dense);
	}
	const Scrim column = ScrimOf(PageKind::Column), dense = ScrimOf(PageKind::Dense);
	for (const Scrim& scrim : {column, dense}) {
		for (float corner : {scrim.topLeft, scrim.topRight, scrim.bottomRight, scrim.bottomLeft}) {
			CHECK(corner > 0.f);
			CHECK(corner <= 1.f);
		}
	}
	// The column's rows sit on the left: darkest there, the open screen on the right.
	CHECK(column.topLeft >= 0.9f);
	CHECK(column.bottomLeft >= 0.9f);
	CHECK(column.topRight < column.topLeft);
	CHECK(column.topRight < dense.topRight);
	// A dense page: even across, never lighter than 80% ink anywhere.
	CHECK(dense.topLeft == dense.topRight);
	CHECK(dense.bottomLeft == dense.bottomRight);
	for (float corner : {dense.topLeft, dense.topRight, dense.bottomRight, dense.bottomLeft}) CHECK(corner >= 0.8f);
	CHECK(std::string(PageKindName(PageKind::Column)) == "column");
	CHECK(std::string(PageKindName(PageKind::Dense)) == "dense");
}

TEST_CASE("a scrim mixes from one page's to the next's, and blends between its corners")
{
	const Scrim column = ScrimOf(PageKind::Column), dense = ScrimOf(PageKind::Dense);
	const auto same = [](const Scrim& a, const Scrim& b) {
		return a.topLeft == doctest::Approx(b.topLeft) && a.topRight == doctest::Approx(b.topRight) &&
			a.bottomRight == doctest::Approx(b.bottomRight) && a.bottomLeft == doctest::Approx(b.bottomLeft);
	};
	CHECK(Mix(column, dense, 0) == column);
	CHECK(same(Mix(column, dense, 1), dense));
	CHECK(same(Mix(column, dense, 5), dense));
	CHECK(Mix(column, dense, -1) == column);
	CHECK(Mix(column, dense, 0.5f).topRight == doctest::Approx((column.topRight + dense.topRight) / 2));
	const Scrim corners{0.1f, 0.2f, 0.3f, 0.4f};
	CHECK(ScrimAt(corners, 0, 0) == doctest::Approx(0.1f));
	CHECK(ScrimAt(corners, 1, 0) == doctest::Approx(0.2f));
	CHECK(ScrimAt(corners, 1, 1) == doctest::Approx(0.3f));
	CHECK(ScrimAt(corners, 0, 1) == doctest::Approx(0.4f));
	CHECK(ScrimAt(corners, 0.5f, 0.5f) == doctest::Approx(0.25f));
	CHECK(ScrimAt(corners, -2, 9) == doctest::Approx(0.4f));
}

TEST_CASE("a picture covers the screen at its own shape")
{
	// 16:9 on 16:9: all of it.
	UvRect uv = CoverFit(1920, 1080, 2560, 1440);
	CHECK(uv.u0 == 0.f);
	CHECK(uv.v0 == 0.f);
	CHECK(uv.u1 == 1.f);
	CHECK(uv.v1 == 1.f);
	// 16:10 and 4:3: the full height, a centred band of the width.
	uv = CoverFit(1920, 1080, 1920, 1200);
	CHECK(uv.v0 == 0.f);
	CHECK(uv.v1 == 1.f);
	CHECK(uv.u1 - uv.u0 == doctest::Approx((1920.f / 1200) / (1920.f / 1080)));
	CHECK(uv.u0 == doctest::Approx(1 - uv.u1));
	uv = CoverFit(1920, 1080, 1024, 768);
	CHECK(uv.u1 - uv.u0 == doctest::Approx(0.75f));
	CHECK(uv.u0 == doctest::Approx(0.125f));
	// 21:9: the full width, a band of the height kept a little high.
	uv = CoverFit(1920, 1080, 3440, 1440);
	CHECK(uv.u0 == 0.f);
	CHECK(uv.u1 == 1.f);
	const float span = (1920.f / 1080) / (3440.f / 1440);
	CHECK(uv.v1 - uv.v0 == doctest::Approx(span));
	CHECK(uv.v0 == doctest::Approx((1 - span) * kAnchorY));
	CHECK(uv.v0 < 1 - uv.v1);
	// The drawn rect keeps the picture's shape: its texels are square on screen.
	const std::vector<std::pair<float, float>> screens = {{1280.f, 720.f}, {1680.f, 1050.f}, {2560.f, 1080.f}, {800.f, 600.f}, {1080.f, 1920.f}};
	for (const auto& [w, h] : screens) {
		const UvRect fit = CoverFit(1920, 1080, w, h);
		const float shownWidth = (fit.u1 - fit.u0) * 1920, shownHeight = (fit.v1 - fit.v0) * 1080;
		CHECK(shownWidth / shownHeight == doctest::Approx(w / h).epsilon(0.001));
		CHECK(fit.u0 >= 0.f);
		CHECK(fit.v0 >= 0.f);
		CHECK(fit.u1 <= 1.0001f);
		CHECK(fit.v1 <= 1.0001f);
	}
	// The anchors place the cut; a size that is not positive is the whole picture.
	CHECK(CoverFit(1920, 1080, 1000, 1000, 0, 0).u0 == 0.f);
	CHECK(CoverFit(1920, 1080, 1000, 1000, 1, 0).u1 == doctest::Approx(1.f));
	uv = CoverFit(0, 1080, 1920, 1080);
	CHECK(uv.u0 == 0.f);
	CHECK(uv.u1 == 1.f);
	uv = CoverFit(1920, 1080, 0, 1080);
	CHECK(uv.v1 == 1.f);
}

TEST_CASE("drawn means drawn in the frame just presented, and goes false once a frame passes without a draw")
{
	DrawWatch watch;
	CHECK_FALSE(watch.DrawnLastFrame());
	CHECK_FALSE(watch.AskedWithin(kAskedSeconds, 0));
	CHECK(watch.IdleFor(kIdleReleaseSeconds, 0));
	// Shown at the menu: asked and drawn each frame.
	double now = 100;
	std::uint64_t frame = 0;
	for (int i = 0; i < 60; i++) {
		watch.Presented(++frame, now += kFrame);
		watch.Asked();
		watch.Drawn();
		CHECK(watch.DrawnLastFrame());
	}
	// The next frame presented, its draw not made yet: the frame just
	// presented was drawn.
	watch.Presented(++frame, now += kFrame);
	CHECK(watch.DrawnLastFrame());
	// A frame without a draw (the shell closed, a battle): no longer drawn,
	// and it stays so however long nothing is drawn.
	watch.Presented(++frame, now += kFrame);
	CHECK_FALSE(watch.DrawnLastFrame());
	for (int i = 0; i < 600; i++) {
		watch.Presented(++frame, now += kFrame);
		CHECK_FALSE(watch.DrawnLastFrame());
	}
}

TEST_CASE("asked for but with nothing to draw yet, the backdrop is wanted, not drawn")
{
	DrawWatch watch;
	watch.Presented(1, 5);
	watch.Asked();
	CHECK_FALSE(watch.DrawnLastFrame());
	CHECK(watch.AskedWithin(kAskedSeconds, 5.5));
	CHECK_FALSE(watch.IdleFor(kIdleReleaseSeconds, 9));
}

TEST_CASE("the loader's gate and the release go by the wall clock, which runs on when nothing is drawn")
{
	DrawWatch watch;
	watch.Presented(10, 50);
	watch.Asked();
	watch.Drawn();
	CHECK(watch.AskedWithin(kAskedSeconds, 50.9));
	CHECK(watch.AskedWithin(kAskedSeconds, 51.0));
	CHECK_FALSE(watch.AskedWithin(kAskedSeconds, 51.1));
	CHECK_FALSE(watch.IdleFor(kIdleReleaseSeconds, 54.9));
	CHECK(watch.IdleFor(kIdleReleaseSeconds, 55.0));
	// Shown again: wanted again, not idle.
	watch.Presented(400, 60);
	watch.Asked();
	CHECK(watch.AskedWithin(kAskedSeconds, 60.5));
	CHECK_FALSE(watch.IdleFor(kIdleReleaseSeconds, 61));
	CHECK(kAskedSeconds <= 1.0);
	CHECK(kIdleReleaseSeconds > kAskedSeconds);
}

TEST_CASE("in a battle nothing loads, nothing grows, and the pictures go once it has not been shown for a while")
{
	// As SfvBackdrop runs the two: the shell's frames ask for the backdrop and
	// move the cycle; the loader loads only while the frame allows it and it
	// was asked within the last second; a frame finding it idle lets
	// everything go.
	Cycle cycle(8, 5, Timing{4, 0.5, 0.5, 0.5});
	DrawWatch watch;
	std::set<int> alive;
	double now = 0;
	std::uint64_t frame = 0;
	int loads = 0;
	const auto step = [&](bool shown) {
		watch.Presented(++frame, now += kFrame);
		if ((cycle.Current() >= 0 || cycle.NextReady()) && watch.IdleFor(kIdleReleaseSeconds, now)) cycle.Reset();
		for (int entry : cycle.TakeReleases()) CHECK(alive.erase(entry) == 1);
		if (shown && watch.AskedWithin(kAskedSeconds, now)) {
			if (const std::optional<int> wanted = cycle.Wanted()) {
				cycle.Started(*wanted);
				loads++;
				if (cycle.Loaded(*wanted, true)) alive.insert(*wanted);
			}
		}
		if (shown) {
			watch.Asked();
			cycle.Advance(kFrame);
			for (int entry : cycle.TakeReleases()) CHECK(alive.erase(entry) == 1);
			if (!cycle.Shown().Empty()) watch.Drawn();
		}
		CHECK(cycle.Alive() <= 2);
	};
	for (int i = 0; i < 60 * 20; i++) step(true);
	REQUIRE(cycle.Current() >= 0);
	const int aliveAtStart = cycle.Alive();
	// The battle: frames presented, the backdrop never asked for.
	const int loadsAtStart = loads;
	for (int i = 0; i < 60 * 30; i++) {
		step(false);
		CHECK(cycle.Alive() <= aliveAtStart);
		CHECK_FALSE(watch.DrawnLastFrame());
	}
	CHECK(loads == loadsAtStart);
	CHECK(cycle.Alive() == 0);
	CHECK(alive.empty());
	CHECK(cycle.Shown().Empty());
	// Back at the menu: the next picture loads and fades in.
	for (int i = 0; i < 60 * 5; i++) step(true);
	CHECK(cycle.Current() >= 0);
	CHECK(watch.DrawnLastFrame());
}
