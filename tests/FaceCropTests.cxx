// The square a fighter's small face is drawn from (SfvFaceCrop, EMBER_LIBRARY.md
// section 37): inside the select grid's slanted card, never over its
// transparent corners or its antialiased edge, and centred on the face.

#include <doctest/doctest.h>

#include "ember/SfvFaceCrop.hxx"

#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace sfve::adapter;

namespace {

constexpr int kWidth = 116, kHeight = 144, kSlant = 29;

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

// SFV's select grid card: each row 87 px painted, leaning right towards the
// top, with a half-transparent pixel at either end of the row as the slant's
// antialiasing leaves it.
Picture SlantedCard(std::size_t pitch = kWidth * 4, std::uint8_t padding = 0)
{
	Picture card(kWidth, kHeight, pitch, padding);
	for (int y = 0; y < kHeight; y++) {
		const int left = kSlant - kSlant * y / (kHeight - 1), right = left + kWidth - kSlant;
		for (int x = left; x < right; x++) card.Alpha(x, y) = 255;
		if (left > 0) card.Alpha(left - 1, y) = 128;
		if (right < kWidth) card.Alpha(right, y) = 128;
	}
	return card;
}

} // namespace

TEST_CASE("a slanted card's face is the largest square of its solid paint, centred on the face")
{
	Picture card = SlantedCard();
	const std::optional<SquareCrop> face = card.Face();
	REQUIRE(face);
	CHECK(face->side == 73);
	for (int y = face->y; y < face->y + face->side; y++)
		for (int x = face->x; x < face->x + face->side; x++) REQUIRE(card.Alpha(x, y) == 255);
	// Its centre two fifths down the card, where the game holds the face.
	CHECK(std::abs(face->y + face->side / 2 - kHeight * 2 / 5) <= 1);
}

TEST_CASE("a picture painted to its edges gives its widest square, kept inside it")
{
	Picture whole(kWidth, kHeight, kWidth * 4);
	for (int y = 0; y < kHeight; y++)
		for (int x = 0; x < kWidth; x++) whole.Alpha(x, y) = 255;
	const std::optional<SquareCrop> face = whole.Face();
	REQUIRE(face);
	CHECK(face->x == 0);
	CHECK(face->y == 0); // centred two fifths down it would start above the picture
	CHECK(face->side == kWidth);
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

TEST_CASE("a picture with no square half its width painted has no face, and is drawn whole")
{
	Picture empty(kWidth, kHeight, kWidth * 4);
	CHECK_FALSE(empty.Face());
	Picture strip(kWidth, kHeight, kWidth * 4);
	for (int y = 0; y < kHeight; y++)
		for (int x = 40; x < 90; x++) strip.Alpha(x, y) = 255;
	CHECK_FALSE(strip.Face());
	// Half-transparent paint is the card's edge, never its ground.
	Picture faint(kWidth, kHeight, kWidth * 4);
	for (int y = 0; y < kHeight; y++)
		for (int x = 0; x < kWidth; x++) faint.Alpha(x, y) = 200;
	CHECK_FALSE(faint.Face());
	CHECK_FALSE(FaceSquare(nullptr, kWidth, kHeight, kWidth * 4));
}
