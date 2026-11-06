// Ember's match HUD laid out around SFV's battle HUD (EMBER_LIBRARY.md section
// 18): SFV Ember's product describes that HUD (SfvProduct.cxx), and wherever
// the player's settings put Ember's panel, on any screen, nothing of Ember's
// covers SFV's bars, timer, portraits, names or gauges.

#include <doctest/doctest.h>

#include "common/ProductIdentity.hxx"
#include "ui/Theme.hxx"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct Box {
	float x0, y0, x1, y1;
	const char* what;
};

// SFV's battle HUD in its 1280x720 frame, as measured on its fights (each a
// pixel or two wider than the art), and its mirror for P2.
std::vector<Box> SfvHud()
{
	std::vector<Box> left = {
		{103, 54, 596, 82, "life bar"},
		{0, 0, 145, 96, "portrait"},
		{113, 83, 200, 100, "fighter name"},
		{444, 83, 596, 97, "stun gauge"},
		{60, 624, 232, 660, "V-Skill and V-gauge"},
		{88, 660, 370, 688, "EX gauge"},
	};
	std::vector<Box> all = {{606, 40, 674, 95, "timer"}};
	for (const Box& b : left) {
		all.push_back(b);
		all.push_back({1280 - b.x1, b.y0, 1280 - b.x0, b.y1, b.what});
	}
	return all;
}

// SFV keeps 16:9 in any window: its picture, and the HUD's 720p units in it.
struct Frame {
	float x0, y0, scale;
};
Frame FrameOn(float width, float height)
{
	const float s = (std::min)(width / 1280.f, height / 720.f);
	return {(width - 1280 * s) * .5f, (height - 720 * s) * .5f, s};
}

bool Overlap(const sf4e::ui::MatchStripBox& a, const Box& b, const Frame& f)
{
	const float x0 = f.x0 + b.x0 * f.scale, x1 = f.x0 + b.x1 * f.scale, y0 = f.y0 + b.y0 * f.scale, y1 = f.y0 + b.y1 * f.scale;
	return a.valid && a.x0 < x1 && x0 < a.x1 && a.y0 < y1 && y0 < a.y1;
}

std::string Name(const sf4e::ui::MatchStripBox& b)
{
	return std::to_string((int)b.x0) + "," + std::to_string((int)b.y0) + " - " + std::to_string((int)b.x1) + "," + std::to_string((int)b.y1);
}

struct Fonts {
	Fonts()
	{
		ImGui::CreateContext();
		sf4e::ui::ApplyTheme(1.f, 1.f);
		ImGui::GetIO().Fonts->Build();
	}
	~Fonts() { ImGui::DestroyContext(); }
};

} // namespace

TEST_CASE("SFV Ember describes SFV's battle HUD, and its plates carry the names")
{
	const ember::BattleHud* hud = ember::Product().branding.battleHud;
	REQUIRE(hud != nullptr);
	CHECK(hud->frameWidth == 1280.f);
	CHECK(hud->frameHeight == 720.f);
	CHECK(hud->namesOnPlates);
	CHECK_FALSE(hud->nameOffset);
	// The marks of each side are the mirror of the other's.
	CHECK(hud->marks[1].x0 == 1280.f - hud->marks[0].x1);
	CHECK(hud->marks[1].x1 == 1280.f - hud->marks[0].x0);
	CHECK(hud->plate[1].x0 == 1280.f - hud->plate[0].x1);
}

TEST_CASE("Ember's match HUD stays on screen and clear of SFV's HUD wherever it is put")
{
	Fonts fonts;
	const std::vector<Box> sfv = SfvHud();
	const float screens[][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {2560, 1080}};
	int laid = 0;
	for (const auto& screen : screens) {
		const Frame frame = FrameOn(screen[0], screen[1]);
		for (int layout = 0; layout < 2; ++layout)
			for (int anchor = 0; anchor <= 5; ++anchor)
				for (int size = 0; size < 3; ++size)
					for (int raised = 0; raised < 2; ++raised)
						for (int busy = 0; busy < 2; ++busy) {
							sf4e::ui::MatchStripView view;
							view.layout = layout;
							view.anchor = anchor;
							view.size = size;
							view.raised = raised != 0;
							view.names[0] = "Longest-Name-Here";
							view.names[1] = "Akira";
							view.pingMs = 999;
							view.rollbackFrames = 12;
							view.appliedDelay = 9;
							if (busy) {
								// Everything the panel can hold: the score, watchers and a notice line.
								view.score = "12 - 11";
								view.spectators = 12;
								view.notice = "Connection unstable. Waiting for the other player to catch up before the game goes on.";
								view.noticeSeverity = 1;
								view.badges[0] = sf4e::SkillBadge::OfClass(6, true);
								view.badges[1] = sf4e::SkillBadge::OfClass(9, false);
							}
							const auto bounds = sf4e::ui::MatchStripGeometry(view, ImVec2(0, 0), ImVec2(screen[0], screen[1]));
							INFO("screen " << screen[0] << "x" << screen[1] << ", layout " << layout << ", anchor " << anchor << ", size "
								<< size << ", raised " << raised << ", busy " << busy);
							std::vector<std::pair<const char*, sf4e::ui::MatchStripBox>> ours = {{"panel", bounds.panel}, {"state line", bounds.state}};
							if (layout == 1) {
								ours.push_back({"P1 marks", bounds.names[0]});
								ours.push_back({"P2 marks", bounds.names[1]});
							}
							CHECK(bounds.panel.valid);
							for (const auto& [what, box] : ours) {
								if (!box.valid) continue;
								INFO(what << " at " << Name(box));
								CHECK(box.x0 >= 0);
								CHECK(box.y0 >= 0);
								CHECK(box.x1 <= screen[0]);
								CHECK(box.y1 <= screen[1]);
								for (const Box& b : sfv) {
									INFO("SFV's " << b.what);
									CHECK_FALSE(Overlap(box, b, frame));
								}
							}
							++laid;
						}
	}
	CHECK(laid == 5 * 2 * 6 * 3 * 2 * 2);
}

TEST_CASE("each name's marks sit in the row under its own bar, against its stun gauge")
{
	Fonts fonts;
	sf4e::ui::MatchStripView view;
	view.layout = 1;
	view.badges[0] = sf4e::SkillBadge::Placing(); // drawn as a pill: SFV has no class emblems (USF4's art)
	const auto at720 = sf4e::ui::MatchStripGeometry(view, ImVec2(0, 0), ImVec2(1280, 720));
	REQUIRE(at720.names[0].valid);
	REQUIRE(at720.names[1].valid);
	// P1's ends at the stun gauge's outer end (x 446) less its gap; P2's mirrors it.
	CHECK(at720.names[0].x1 == doctest::Approx(440));
	CHECK(at720.names[1].x0 == doctest::Approx(840));
	CHECK(at720.names[0].y0 == doctest::Approx(83));
	CHECK(at720.names[0].y1 == doctest::Approx(99));
	// The class mark makes P1's chip wider than P2's, which has none.
	CHECK(at720.names[0].x1 - at720.names[0].x0 > at720.names[1].x1 - at720.names[1].x0);
	// At 1080p everything is 1.5 times; at 2560x1080 the picture is pillarboxed by 320.
	const auto at1080 = sf4e::ui::MatchStripGeometry(view, ImVec2(0, 0), ImVec2(1920, 1080));
	CHECK(at1080.names[0].x1 == doctest::Approx(660));
	CHECK(at1080.names[0].y0 == doctest::Approx(124.5));
	const auto wide = sf4e::ui::MatchStripGeometry(view, ImVec2(0, 0), ImVec2(2560, 1080));
	CHECK(wide.names[0].x1 == doctest::Approx(980));
	CHECK(wide.names[1].x0 == doctest::Approx(320 + 1260));
}
