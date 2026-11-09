// The square a fighter's small face is drawn from (SfvFaceCrop, EMBER_LIBRARY.md
// section 37): a little wider than the select grid's slanted strip, centred
// on the strip at the head, with the strip's shape as measured on the game's
// RYU and CHUN-LI cards.

#include <doctest/doctest.h>

#include "ember/SfvFaceCrop.hxx"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace sfve::adapter;

namespace {

constexpr int kWidth = 116, kHeight = 144;

// A picture of B8G8R8A8 pixels, rows `pitch` bytes apart; the bytes past
// each row's pixels are filled with `padding`.
struct Picture {
	int width, height;
	std::size_t pitch;
	std::vector<std::uint8_t> bytes;
	Picture(int w, int h, std::size_t rowPitch, std::uint8_t padding = 0)
		: width(w), height(h), pitch(rowPitch), bytes(rowPitch * (std::size_t)h, padding)
	{
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) Alpha(x, y) = 0;
	}
	std::uint8_t& Alpha(int x, int y) { return bytes[pitch * (std::size_t)y + (std::size_t)x * 4 + 3]; }
	std::optional<SquareCrop> Face() { return FaceSquare(bytes.data(), width, height, pitch); }
};

// The game's select grid card as measured: rows 2 to 141 painted, each 61 px
// wide, its left edge moving from 10 to 32 down the card, with a
// half-transparent pixel at each end as the slant's antialiasing leaves it.
constexpr int kTop = 2, kBottom = 142, kStrip = 61;
int StripLeft(int y) { return 10 + 22 * (y - kTop) / (kBottom - 1 - kTop); }

Picture SlantedCard(std::size_t pitch = kWidth * 4, std::uint8_t padding = 0)
{
	Picture card(kWidth, kHeight, pitch, padding);
	for (int y = kTop; y < kBottom; y++) {
		const int left = StripLeft(y), right = left + kStrip;
		for (int x = left; x < right; x++) card.Alpha(x, y) = 255;
		card.Alpha(left - 1, y) = 128;
		card.Alpha(right, y) = 128;
	}
	return card;
}

} // namespace

TEST_CASE("a slanted card's face is a square a little wider than the strip, centred on it at the head")
{
	Picture card = SlantedCard();
	const std::optional<SquareCrop> face = card.Face();
	REQUIRE(face);
	CHECK(face->side == 69); // 61 x 1.13, as on the game's cards
	// Its middle row is a little below two fifths of the painted rows, where the head is.
	const int middle = face->y + face->side / 2;
	CHECK(std::abs(middle - (kTop + (kBottom - kTop) * 44 / 100)) <= 1);
	// Centred on the strip there.
	CHECK(std::abs(face->x + face->side / 2 - (StripLeft(middle) + kStrip / 2)) <= 1);
	// Only two small corner wedges and the slant's soft edge are unpainted:
	// about an eighth of the square, never more than a sixth.
	int unpainted = 0;
	for (int y = face->y; y < face->y + face->side; y++)
		for (int x = face->x; x < face->x + face->side; x++) unpainted += card.Alpha(x, y) < 248;
	CHECK(unpainted * 6 < face->side * face->side);
}

TEST_CASE("a picture painted to its edges gives its widest square, kept inside it")
{
	Picture whole(kWidth, kHeight, kWidth * 4);
	for (int y = 0; y < kHeight; y++)
		for (int x = 0; x < kWidth; x++) whole.Alpha(x, y) = 255;
	const std::optional<SquareCrop> face = whole.Face();
	REQUIRE(face);
	CHECK(face->x == 0);
	CHECK(face->side == kWidth);
	CHECK(face->y >= 0);
	CHECK(face->y + face->side <= kHeight);
}

TEST_CASE("the face is read row by row at the texture's own pitch")
{
	// Bytes past each row are opaque: read as pixels, they would widen the rows.
	Picture padded = SlantedCard(kWidth * 4 + 64, 255);
	Picture packed = SlantedCard();
	const std::optional<SquareCrop> face = padded.Face(), expected = packed.Face();
	REQUIRE(face);
	REQUIRE(expected);
	CHECK(face->x == expected->x);
	CHECK(face->y == expected->y);
	CHECK(face->side == expected->side);
}

TEST_CASE("a hole in the paint or a ragged top does not move the face")
{
	Picture card = SlantedCard();
	const std::optional<SquareCrop> clean = card.Face();
	// CHUN-LI's card has transparent bits inside it, and RYU's hair spikes above its top rows.
	for (int y = 75; y < 85; y++)
		for (int x = 65; x < 72; x++) card.Alpha(x, y) = 0;
	for (int x = 20; x < 40; x += 4) card.Alpha(x, 0) = 255;
	const std::optional<SquareCrop> marked = card.Face();
	REQUIRE(clean);
	REQUIRE(marked);
	CHECK(std::abs(marked->x - clean->x) <= 1);
	CHECK(std::abs(marked->y - clean->y) <= 1);
	CHECK(marked->side == clean->side);
}

TEST_CASE("a picture with no strip a quarter of its width has no face, and is drawn whole")
{
	Picture empty(kWidth, kHeight, kWidth * 4);
	CHECK_FALSE(empty.Face());
	Picture thin(kWidth, kHeight, kWidth * 4);
	for (int y = 0; y < kHeight; y++)
		for (int x = 40; x < 60; x++) thin.Alpha(x, y) = 255;
	CHECK_FALSE(thin.Face());
	// Half-transparent paint is the card's edge, never its ground.
	Picture faint(kWidth, kHeight, kWidth * 4);
	for (int y = 0; y < kHeight; y++)
		for (int x = 0; x < kWidth; x++) faint.Alpha(x, y) = 200;
	CHECK_FALSE(faint.Face());
	CHECK_FALSE(FaceSquare(nullptr, kWidth, kHeight, kWidth * 4));
}
