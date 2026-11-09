#pragma once

// The square Ember draws a fighter's small face from. SFV's select grid
// face (CharaSelectIconLeft, 116x144) is a slanted strip: the game's artists
// framed each fighter's head in a parallelogram about 61 px wide, leaning
// about 22 px over its height, its corners transparent. Drawn whole in a
// square tile it is a narrow, slanted sliver; the largest square wholly
// inside it (about 51 px) cuts the face from brow to nose. The square here is
// a little wider than the strip (kFrame), centred on the strip at the head,
// so hair to chin fills the tile and only two small slanted corners show the
// tile's ground behind it, as the game's own slanted cards do. Measured on
// the game's RYU and CHUN-LI cards (EMBER_LIBRARY.md section 37). Its pixels
// are the game's own: never stretched, only cut.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace sfve::adapter {

// A square of a picture, in its pixels.
struct SquareCrop {
	int x = 0, y = 0, side = 0;
};

// The face's square in a picture of 4-byte pixels whose fourth byte is alpha
// (B8G8R8A8 or R8G8B8A8), rows `rowPitch` bytes apart. Empty, so the picture
// is drawn whole, when nothing is painted or the strip is under a quarter of
// the width.
std::optional<SquareCrop> FaceSquare(const std::uint8_t* pixels, int width, int height, std::size_t rowPitch);

} // namespace sfve::adapter