#include "ember/SfvFaceCrop.hxx"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sfve::adapter {

namespace {

// A pixel at or above this alpha is the card's painted ground; the slant's
// antialiased edge falls below it.
constexpr std::uint8_t kSolid = 248;
// The square's side as a share of the strip's painted width: wide enough to
// hold a head from hair to chin across the strip's lean, so only two small
// corner wedges are left unpainted.
constexpr float kFrame = 1.13f;
// How far down the painted rows the square's centre sits: the game's cards
// hold the hair near the top and the eyes about two fifths down, so the
// square's centre just below that keeps hair, eyes and chin in.
constexpr float kFaceCentre = .44f;

// A row's painted columns, [left, right); unpainted when left is not below right.
struct Span {
	int left, right;
	bool Painted() const { return left < right; }
	int Width() const { return right - left; }
};

} // namespace

std::optional<SquareCrop> FaceSquare(const std::uint8_t* pixels, int width, int height, std::size_t rowPitch)
{
	if (!pixels || width <= 0 || height <= 0) return std::nullopt;
	std::vector<Span> rows((size_t)height, Span{width, 0});
	std::vector<int> widths;
	int top = height, bottom = 0;
	for (int y = 0; y < height; y++) {
		const std::uint8_t* row = pixels + rowPitch * (size_t)y;
		Span& span = rows[(size_t)y];
		for (int x = 0; x < width; x++) {
			if (row[(size_t)x * 4 + 3] < kSolid) continue;
			span.left = (std::min)(span.left, x);
			span.right = x + 1;
		}
		if (!span.Painted()) continue;
		widths.push_back(span.Width());
		top = (std::min)(top, y);
		bottom = y + 1;
	}
	if (widths.empty()) return std::nullopt;
	// The strip's width: the median row's, so a ragged top (hair) or a hole
	// in the paint moves it no more than a row.
	std::nth_element(widths.begin(), widths.begin() + widths.size() / 2, widths.end());
	const int strip = widths[widths.size() / 2];
	if (strip * 4 < width) return std::nullopt;

	const int side = (std::min)({width, bottom - top, (int)std::lround(strip * kFrame)});
	const int y = (std::clamp)(top + (int)(kFaceCentre * (float)(bottom - top)) - side / 2, top, bottom - side);
	// Centred on the strip at the square's middle row (the nearest painted one).
	const int middle = y + side / 2;
	int nearest = top;
	for (int row = top; row < bottom; row++) {
		if (rows[(size_t)row].Painted() && std::abs(row - middle) < std::abs(nearest - middle)) nearest = row;
	}
	const Span& at = rows[(size_t)nearest];
	const int x = (std::clamp)((at.left + at.right) / 2 - side / 2, 0, width - side);
	return SquareCrop{x, y, side};
}

} // namespace sfve::adapter