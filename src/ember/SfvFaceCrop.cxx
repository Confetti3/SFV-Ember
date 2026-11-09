#include "ember/SfvFaceCrop.hxx"

#include <algorithm>
#include <vector>

namespace sfve::adapter {

namespace {

// A pixel at or above this alpha is the card's painted ground. The slant's
// antialiased edge falls below it and stays outside the square, so no tile
// ever shows through a corner of the face.
constexpr std::uint8_t kSolid = 248;
// How far down the painted rows the square's centre sits. The game's cards
// hold head and shoulders, the hair near the card's top edge and the eyes
// about two fifths down (RYU's and CHUN-LI's, measured on a 38 px tile), so
// a square smaller than the card is centred there, on the face.
constexpr float kFaceCentre = .4f;

// A row's painted columns, [left, right); none when left is not below right.
struct Span {
	int left, right;
};

} // namespace

std::optional<SquareCrop> FaceSquare(const std::uint8_t* pixels, int width, int height, std::size_t rowPitch)
{
	if (!pixels || width <= 0 || height <= 0) return std::nullopt;
	std::vector<Span> rows((size_t)height, Span{width, 0});
	int top = height, bottom = 0;
	for (int y = 0; y < height; y++) {
		const std::uint8_t* row = pixels + rowPitch * (size_t)y;
		Span& span = rows[(size_t)y];
		for (int x = 0; x < width; x++) {
			if (row[(size_t)x * 4 + 3] < kSolid) continue;
			span.left = (std::min)(span.left, x);
			span.right = x + 1;
		}
		if (span.left < span.right) {
			top = (std::min)(top, y);
			bottom = y + 1;
		}
	}
	// The largest square first: the narrowest span among the rows it covers
	// must hold it. A row with nothing painted holds none.
	for (int side = (std::min)(width, bottom - top); side >= (width + 1) / 2; side--) {
		const int centred = top + (int)(kFaceCentre * (float)(bottom - top)) - side / 2;
		const int y = (std::clamp)(centred, top, bottom - side);
		int left = 0, right = width;
		for (int row = y; row < y + side; row++) {
			left = (std::max)(left, rows[(size_t)row].left);
			right = (std::min)(right, rows[(size_t)row].right);
		}
		if (right - left >= side) return SquareCrop{left + (right - left - side) / 2, y, side};
	}
	return std::nullopt;
}

} // namespace sfve::adapter
