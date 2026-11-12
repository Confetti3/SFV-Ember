// The photo booth's pure parts (mod/BoothMode, overlay/FrameGrab), checked
// without the game: the booth commands and their scripted steps as the pipe
// protocol writes them (docs/design/beta8/booth-protocol.md), the BMP a grab
// writes from each back buffer format, the depth target a grab copies and
// the words its depth file holds.

#include <doctest/doctest.h>

#include "mod/BoothMode.hxx"
#include "mod/GameInput.hxx"
#include "mod/SfvOffsets.hxx"
#include "overlay/FrameGrab.hxx"

#include <dxgiformat.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace sfve::mod;
namespace grab = sfve::overlay::grab;

namespace {

std::optional<booth::Command> Parse(const char* args)
{
	std::string error;
	std::optional<booth::Command> command = booth::ParseCommand(args, error);
	if (!command) CHECK_FALSE(error.empty());
	return command;
}

std::uint32_t U32(const std::vector<std::uint8_t>& bytes, size_t at)
{
	return (std::uint32_t)bytes[at] | ((std::uint32_t)bytes[at + 1] << 8) | ((std::uint32_t)bytes[at + 2] << 16) |
		((std::uint32_t)bytes[at + 3] << 24);
}

std::uint16_t U16(const std::vector<std::uint8_t>& bytes, size_t at)
{
	return (std::uint16_t)(bytes[at] | (bytes[at + 1] << 8));
}

constexpr size_t kPixels = 14 + 108; // where a grab's BMP's pixels start

grab::DepthCandidate Candidate(int index, int width, int height, std::uint32_t draws, int samples = 1)
{
	grab::DepthCandidate c;
	c.index = index;
	c.format = "R24G8_TYPELESS";
	c.width = width;
	c.height = height;
	c.samples = samples;
	c.draws = draws;
	return c;
}

} // namespace

TEST_CASE("a booth command starts with nothing to do")
{
	const booth::Command command;
	CHECK(command.verb == booth::Verb::Hold);
	CHECK(command.steps.empty());
	CHECK(command.scale[0] == 1.f);
	CHECK(command.scale[1] == 1.f);
	CHECK(command.grab.depth.mode == grab::DepthMode::Auto);
}

TEST_CASE("scripted steps: buttons joined by +, a frame count after :, steps by commas")
{
	std::vector<booth::Step> steps;
	std::string error;
	REQUIRE(booth::ParseSteps("MP+MK:4,RIGHT:40", steps, error));
	REQUIRE(steps.size() == 2);
	CHECK(steps[0].buttons == (input::kMP | input::kMK)); // V-Skill
	CHECK(steps[0].frames == 4);
	CHECK(steps[1].buttons == input::kRight);
	CHECK(steps[1].frames == 40);
	// Case does not matter; a count defaults to 1; NONE presses nothing.
	REQUIRE(booth::ParseSteps("hp+hk,none:10,Down+Right:2", steps, error));
	REQUIRE(steps.size() == 3);
	CHECK(steps[0].buttons == (input::kHP | input::kHK)); // V-Trigger
	CHECK(steps[0].frames == 1);
	CHECK(steps[1].buttons == 0);
	CHECK(steps[1].frames == 10);
	CHECK(steps[2].buttons == (input::kDown | input::kRight));
	CHECK(booth::StepFrames(steps) == 13);
	// Every button the protocol names.
	REQUIRE(booth::ParseSteps("UP,DOWN,LEFT,RIGHT,LP,MP,HP,LK,MK,HK", steps, error));
	CHECK(steps.size() == 10);
	CHECK(steps[4].buttons == input::kLP);
	CHECK(steps[9].buttons == input::kHK);

	// Anything else is refused, and the steps are left as they were.
	const std::vector<booth::Step> before = steps;
	for (const char* bad : {"", "MP+", "+MP", "MP:0", "MP:-1", "MP:x", "MP:", "XP", "MP MK", "MP,,MK", "MP,", "NONE+LP",
			 "LEFT+RIGHT", "UP+DOWN", "MP:999999", "MP:+4"}) {
		error.clear();
		CHECK_FALSE(booth::ParseSteps(bad, steps, error));
		CHECK_FALSE(error.empty());
		CHECK(steps.size() == before.size());
	}
}

TEST_CASE("steps play one after another, then nothing")
{
	std::vector<booth::Step> steps;
	std::string error;
	REQUIRE(booth::ParseSteps("MP+MK:2,RIGHT:3", steps, error));
	CHECK_FALSE(booth::StepAt(steps, -1));
	CHECK(booth::StepAt(steps, 0) == (input::kMP | input::kMK));
	CHECK(booth::StepAt(steps, 1) == (input::kMP | input::kMK));
	CHECK(booth::StepAt(steps, 2) == input::kRight);
	CHECK(booth::StepAt(steps, 4) == input::kRight);
	CHECK_FALSE(booth::StepAt(steps, 5));
	CHECK_FALSE(booth::StepAt({}, 0));
}

TEST_CASE("booth commands as the protocol writes them")
{
	std::optional<booth::Command> c = Parse("hold 600");
	REQUIRE(c);
	CHECK(c->verb == booth::Verb::Hold);
	CHECK(c->frame == 600);
	REQUIRE((c = Parse("hold 0")));
	CHECK(c->frame == 0);
	REQUIRE((c = Parse("run 12")));
	CHECK(c->verb == booth::Verb::Run);
	CHECK(c->frame == 12);
	REQUIRE((c = Parse("play 1 MP+MK:4,RIGHT:40")));
	CHECK(c->verb == booth::Verb::Play);
	CHECK(c->side == 1);
	REQUIRE(c->steps.size() == 2);
	CHECK(c->steps[1].frames == 40);
	REQUIRE((c = Parse("scale 0.001 1")));
	CHECK(c->verb == booth::Verb::Scale);
	CHECK(c->scale[0] == doctest::Approx(0.001f));
	CHECK(c->scale[1] == 1.f);
	REQUIRE((c = Parse("depth")));
	CHECK(c->verb == booth::Verb::Depth);
	REQUIRE((c = Parse("leave")));
	CHECK(c->verb == booth::Verb::Leave);
	REQUIRE((c = Parse("hudlog on")));
	CHECK(c->verb == booth::Verb::HudLog);
	CHECK(c->on);
	REQUIRE((c = Parse("hudlog off")));
	CHECK_FALSE(c->on);
	REQUIRE((c = Parse("hudhide on")));
	CHECK(c->verb == booth::Verb::HudHide);
	CHECK(c->on);
	REQUIRE((c = Parse("hudhide off")));
	CHECK_FALSE(c->on);
	REQUIRE((c = Parse("hud SetVisiblePlayerInfo 0")));
	CHECK(c->verb == booth::Verb::Hud);
	CHECK(c->method == "SetVisiblePlayerInfo");
	CHECK(c->number == 0.f);
	CHECK(c->text.empty());
	// The text is the rest of the line, spaces and all.
	REQUIRE((c = Parse("hud SetPilotName 1 PLAYER  TWO")));
	CHECK(c->number == 1.f);
	CHECK(c->text == "PLAYER  TWO");
	REQUIRE((c = Parse("hud SetWins -1.5")));
	CHECK(c->number == doctest::Approx(-1.5f));
}

TEST_CASE("a grab: its path, how many presents later, and which depth")
{
	std::optional<booth::Command> c = Parse("grab D:/renders/raw/a.bmp");
	REQUIRE(c);
	CHECK(c->verb == booth::Verb::Grab);
	CHECK(c->grab.path == "D:/renders/raw/a.bmp");
	CHECK(c->grab.afterPresents == 0); // the next present
	CHECK(c->grab.depth.mode == grab::DepthMode::Auto);
	REQUIRE((c = Parse("grab b.bmp after=12 depth=off")));
	CHECK(c->grab.afterPresents == 12);
	CHECK(c->grab.depth.mode == grab::DepthMode::Off);
	REQUIRE((c = Parse("grab b.bmp depth=3 after=0")));
	CHECK(c->grab.depth.mode == grab::DepthMode::Index);
	CHECK(c->grab.depth.index == 3);
	REQUIRE((c = Parse("grab \"C:/my renders/a b.bmp\" depth=auto")));
	CHECK(c->grab.path == "C:/my renders/a b.bmp");
	CHECK(c->grab.depth.mode == grab::DepthMode::Auto);

	for (const char* bad : {"grab", "grab a.bmp after=", "grab a.bmp after=-1", "grab a.bmp after=x", "grab a.bmp depth=all",
			 "grab a.bmp depth=-2", "grab a.bmp size=2", "grab \"a.bmp", "grab \"\"", "grab \"a\"b"}) {
		CAPTURE(bad);
		CHECK_FALSE(Parse(bad));
	}
}

TEST_CASE("a malformed booth command is refused with a reason")
{
	for (const char* bad : {"", "   ", "freeze 3", "Hold 3", "hold", "hold -1", "hold x", "hold 3 4", "hold 1.5", "run 0", "run",
			 "play 2 MP", "play 0", "play 0 MP extra", "play x MP", "scale 1", "scale 0 1", "scale -1 1", "scale 1 inf",
			 "scale nan 1", "scale 1 1 1", "depth now", "leave now", "hudlog", "hudlog yes", "hud", "hud SetWins",
			 "hud SetWins x", "hud Set-Wins 1", "hud SetWins nan", "hudhide", "hudhide 1", "hudhide on off"}) {
		CAPTURE(bad);
		CHECK_FALSE(Parse(bad));
	}
}

TEST_CASE("a depth choice is auto, off or a target's number")
{
	grab::DepthChoice choice;
	CHECK(grab::ParseDepthChoice("auto", choice));
	CHECK(choice.mode == grab::DepthMode::Auto);
	CHECK(grab::ParseDepthChoice("off", choice));
	CHECK(choice.mode == grab::DepthMode::Off);
	CHECK(grab::ParseDepthChoice("all", choice));
	CHECK(choice.mode == grab::DepthMode::All);
	CHECK(grab::ParseDepthChoice("0", choice));
	CHECK(choice.mode == grab::DepthMode::Index);
	CHECK(choice.index == 0);
	CHECK(grab::ParseDepthChoice("12", choice));
	CHECK(choice.index == 12);
	for (const char* bad : {"", "-1", "+1", "x", "1x", "AUTO", "99999"}) {
		CAPTURE(bad);
		CHECK_FALSE(grab::ParseDepthChoice(bad, choice));
		CHECK(choice.index == 12); // untouched
	}
}

TEST_CASE("the back buffer formats a grab encodes")
{
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_R8G8B8A8_UNORM) == grab::PixelFormat::R8G8B8A8);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) == grab::PixelFormat::R8G8B8A8);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_R8G8B8A8_TYPELESS) == grab::PixelFormat::R8G8B8A8);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_B8G8R8A8_UNORM) == grab::PixelFormat::B8G8R8A8);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) == grab::PixelFormat::B8G8R8A8);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_B8G8R8A8_TYPELESS) == grab::PixelFormat::B8G8R8A8);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_R10G10B10A2_UNORM) == grab::PixelFormat::R10G10B10A2);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_R10G10B10A2_TYPELESS) == grab::PixelFormat::R10G10B10A2);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_R16G16B16A16_FLOAT) == grab::PixelFormat::Unsupported);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_B8G8R8X8_UNORM) == grab::PixelFormat::Unsupported);
	CHECK(grab::PixelFormatOf(DXGI_FORMAT_UNKNOWN) == grab::PixelFormat::Unsupported);
}

TEST_CASE("a grab's BMP: a 32-bit top-down picture with its alpha, rows in order, BGRA from each format")
{
	// 2x2, rows 12 bytes apart (4 bytes of padding the BMP leaves out).
	// R8G8B8A8: red, green / blue, half-white.
	const std::uint8_t rgba[24] = {255, 0, 0, 255, 0, 255, 0, 128, 0xEE, 0xEE, 0xEE, 0xEE,
		0, 0, 255, 255, 128, 128, 128, 64, 0xEE, 0xEE, 0xEE, 0xEE};
	const std::vector<std::uint8_t> bmp = grab::EncodeBmp(rgba, 2, 2, 12, grab::PixelFormat::R8G8B8A8);
	REQUIRE(bmp.size() == kPixels + 16);
	CHECK(bmp[0] == 'B');
	CHECK(bmp[1] == 'M');
	CHECK(U32(bmp, 2) == bmp.size());     // file size
	CHECK(U32(bmp, 10) == kPixels);       // pixels' offset
	CHECK(U32(bmp, 14) == 108);           // BITMAPV4HEADER
	CHECK(U32(bmp, 18) == 2);             // width
	CHECK((std::int32_t)U32(bmp, 22) == -2); // negative height: top-down
	CHECK(U16(bmp, 26) == 1);             // planes
	CHECK(U16(bmp, 28) == 32);            // bits
	CHECK(U32(bmp, 30) == 3);             // BI_BITFIELDS
	CHECK(U32(bmp, 34) == 16);            // image size
	CHECK(U32(bmp, 54) == 0x00FF0000u);   // red mask
	CHECK(U32(bmp, 58) == 0x0000FF00u);   // green mask
	CHECK(U32(bmp, 62) == 0x000000FFu);   // blue mask
	CHECK(U32(bmp, 66) == 0xFF000000u);   // alpha mask
	CHECK(U32(bmp, 70) == 0x73524742u);   // sRGB
	const std::uint8_t expected[16] = {0, 0, 255, 255, 0, 255, 0, 128, 255, 0, 0, 255, 128, 128, 128, 64};
	CHECK(std::memcmp(bmp.data() + kPixels, expected, sizeof(expected)) == 0);

	// B8G8R8A8: copied as it is, the padding still left out.
	const std::uint8_t bgra[24] = {1, 2, 3, 4, 5, 6, 7, 8, 0xEE, 0xEE, 0xEE, 0xEE, 9, 10, 11, 12, 13, 14, 15, 16, 0xEE, 0xEE, 0xEE, 0xEE};
	const std::vector<std::uint8_t> copied = grab::EncodeBmp(bgra, 2, 2, 12, grab::PixelFormat::B8G8R8A8);
	REQUIRE(copied.size() == kPixels + 16);
	const std::uint8_t same[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	CHECK(std::memcmp(copied.data() + kPixels, same, sizeof(same)) == 0);
}

TEST_CASE("a 10-bit back buffer's channels are scaled to 8 bits")
{
	// R in bits 0-9, G 10-19, B 20-29, A 30-31.
	const auto pack = [](std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a) {
		return r | (g << 10) | (b << 20) | (a << 30);
	};
	const std::uint32_t texels[2] = {pack(1023, 0, 512, 3), pack(0, 1023, 0, 1)};
	std::uint8_t bytes[8];
	std::memcpy(bytes, texels, sizeof(bytes));
	const std::vector<std::uint8_t> bmp = grab::EncodeBmp(bytes, 2, 1, 8, grab::PixelFormat::R10G10B10A2);
	REQUIRE(bmp.size() == kPixels + 8);
	const std::uint8_t* p = bmp.data() + kPixels;
	CHECK(p[0] == 128); // B 512 -> 128
	CHECK(p[1] == 0);
	CHECK(p[2] == 255); // R 1023
	CHECK(p[3] == 255); // A 3
	CHECK(p[4] == 0);
	CHECK(p[5] == 255);
	CHECK(p[6] == 0);
	CHECK(p[7] == 85); // A 1
}

TEST_CASE("a picture a grab cannot encode is no BMP")
{
	const std::uint8_t pixel[8] = {};
	CHECK(grab::EncodeBmp(nullptr, 1, 1, 4, grab::PixelFormat::B8G8R8A8).empty());
	CHECK(grab::EncodeBmp(pixel, 0, 1, 4, grab::PixelFormat::B8G8R8A8).empty());
	CHECK(grab::EncodeBmp(pixel, 1, -1, 4, grab::PixelFormat::B8G8R8A8).empty());
	CHECK(grab::EncodeBmp(pixel, 2, 1, 7, grab::PixelFormat::B8G8R8A8).empty()); // pitch shorter than a row
	CHECK(grab::EncodeBmp(pixel, 1, 1, 4, grab::PixelFormat::Unsupported).empty());
	CHECK(grab::EncodeBmp(pixel, 1, 1, 4, grab::PixelFormat::B8G8R8A8).size() == kPixels + 4);
}

TEST_CASE("the depth target a grab copies")
{
	// A shadow map with more draws than the scene, the scene's depth at the
	// back buffer's size, a small one, and a multisampled one at that size.
	const std::vector<grab::DepthCandidate> targets = {Candidate(0, 2048, 2048, 900), Candidate(1, 1920, 1080, 400),
		Candidate(2, 960, 540, 20), Candidate(3, 1920, 1080, 800, 4)};
	grab::DepthChoice choice;
	CHECK(grab::ChooseDepthTarget(targets, choice, 1920, 1080) == 1); // auto: the back buffer's size first
	CHECK(grab::ChooseDepthTarget(targets, choice, 1280, 720) == 0);  // none that size: the most draws
	choice.mode = grab::DepthMode::Index;
	choice.index = 2;
	CHECK(grab::ChooseDepthTarget(targets, choice, 1920, 1080) == 2);
	choice.index = 4;
	CHECK(grab::ChooseDepthTarget(targets, choice, 1920, 1080) == -1);
	choice.mode = grab::DepthMode::Off;
	CHECK(grab::ChooseDepthTarget(targets, choice, 1920, 1080) == -1);
	// Nothing drawn: no target; a tie goes to the first used.
	CHECK(grab::ChooseDepthTarget({Candidate(0, 1920, 1080, 0)}, grab::DepthChoice{}, 1920, 1080) == -1);
	CHECK(grab::ChooseDepthTarget({}, grab::DepthChoice{}, 1920, 1080) == -1);
	CHECK(grab::ChooseDepthTarget({Candidate(0, 1920, 1080, 5), Candidate(1, 1920, 1080, 5)}, grab::DepthChoice{}, 1920, 1080) == 0);

	const std::string json = grab::DepthTargetsJson({Candidate(0, 1920, 1080, 123)});
	CHECK(json == "{\"targets\":[{\"index\":0,\"format\":\"R24G8_TYPELESS\",\"width\":1920,\"height\":1080,\"samples\":1,"
				  "\"draws\":123,\"id\":0,\"firstDraw\":0,\"lastDraw\":0,\"clears\":0,\"clearDepth\":-1.000000}]}");
	CHECK(grab::DepthTargetsJson({}) == "{\"targets\":[]}");
}

TEST_CASE("a depth file holds one 32-bit word per texel, rows packed")
{
	std::vector<std::uint32_t> words;
	std::string kind;
	// R24G8: as it is (rows 12 bytes apart for 2 texels).
	const std::uint32_t r24[6] = {0x01ABCDEF, 0x02000001, 0xEEEEEEEE, 0x03FFFFFF, 0x04000000, 0xEEEEEEEE};
	REQUIRE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(r24), 2, 2, 12, DXGI_FORMAT_R24G8_TYPELESS, words, kind));
	CHECK(kind == "unorm24_stencil8");
	CHECK((words == std::vector<std::uint32_t>{0x01ABCDEF, 0x02000001, 0x03FFFFFF, 0x04000000}));
	REQUIRE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(r24), 2, 2, 12, DXGI_FORMAT_D24_UNORM_S8_UINT, words, kind));
	CHECK(words.size() == 4);
	// D32_FLOAT: the float's bits.
	const float depth[2] = {1.0f, 0.25f};
	REQUIRE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(depth), 2, 1, 8, DXGI_FORMAT_D32_FLOAT, words, kind));
	CHECK(kind == "float32");
	CHECK((words == std::vector<std::uint32_t>{0x3F800000u, 0x3E800000u}));
	// R32G8X24: the first 4 bytes of each 8-byte texel, the float depth.
	const std::uint32_t wide[4] = {0x3F800000u, 0x000000AAu, 0x3E800000u, 0x000000BBu};
	REQUIRE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(wide), 2, 1, 16, DXGI_FORMAT_R32G8X24_TYPELESS, words, kind));
	CHECK(kind == "float32");
	CHECK((words == std::vector<std::uint32_t>{0x3F800000u, 0x3E800000u}));
	// R16: zero-extended.
	const std::uint16_t r16[2] = {0xFFFF, 0x1234};
	REQUIRE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(r16), 2, 1, 4, DXGI_FORMAT_D16_UNORM, words, kind));
	CHECK(kind == "unorm16");
	CHECK((words == std::vector<std::uint32_t>{0xFFFFu, 0x1234u}));
	// Not a depth format, a short pitch, no size: nothing.
	words.clear();
	CHECK_FALSE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(r24), 2, 2, 12, DXGI_FORMAT_R8G8B8A8_UNORM, words, kind));
	CHECK_FALSE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(r24), 2, 2, 7, DXGI_FORMAT_R24G8_TYPELESS, words, kind));
	CHECK_FALSE(grab::DepthWords(reinterpret_cast<const std::uint8_t*>(r24), 0, 2, 12, DXGI_FORMAT_R24G8_TYPELESS, words, kind));
	CHECK_FALSE(grab::DepthWords(nullptr, 2, 2, 12, DXGI_FORMAT_R24G8_TYPELESS, words, kind));
	CHECK(words.empty());
}

TEST_CASE("a grab needs a path, and one waits at a time")
{
	std::string error;
	grab::Request request;
	CHECK_FALSE(grab::QueueGrab(request, error));
	CHECK(error == "no path");
	// No present ever comes in the tests: the first stays queued, and the
	// next is refused as the protocol says.
	request.path = "photo-booth-test.bmp";
	error.clear();
	REQUIRE(grab::QueueGrab(request, error));
	CHECK(grab::CurrentStatus().pending);
	CHECK(grab::CurrentStatus().presents == 0);
	CHECK_FALSE(grab::QueueGrab(request, error));
	CHECK(error == "grab pending");
	CHECK(grab::DepthReport() == "{\"targets\":[]}");
}

TEST_CASE("the HUD switch's sites are the ones the exe was read for, and in the build table")
{
	CHECK(sfv::kHudPartPostUpdate.rva == 0x41A910);
	CHECK(sfv::kMovieSetVisible.rva == 0xEEE8C0);
	CHECK(sfv::hudpart::kMoviePlayer == 0x318);
	const GameBuild* build = sfv::VerifiedBuild();
	for (const CodeSite* site : {&sfv::kHudPartPostUpdate, &sfv::kMovieSetVisible}) {
		CHECK(std::any_of(build->sites, build->sites + build->siteCount, [site](const CodeSite& listed) { return listed.rva == site->rva; }));
	}
}
