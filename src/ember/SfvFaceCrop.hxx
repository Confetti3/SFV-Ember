#pragma once

// The square Ember draws a fighter's small face from. SFV's select grid
// face (CharaSelectIconLeft, 116x144) is a slanted card: the game's artists
// framed each fighter's head in a parallelogram whose corners are
// transparent. Drawn whole in a square tile it is a narrow, slanted sliver.
// The square here is the largest one wholly inside the card's painted area,
// centred on the face where the game's cards hold it, so a tile shows the
// face the game framed, upright and filling the tile, for every fighter
// alike. Its pixels are the game's own: never stretched, only cut.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace sfve::adapter {

// A square of a picture, in its pixels.
struct SquareCrop {
	int x = 0, y = 0, side = 0;
};

// The face's square in a picture of 4-byte pixels whose fourth byte is alpha
// (B8G8R8A8 or R8G8B8A8), rows `rowPitch` bytes apart. Empty when the
// painted area holds no square at least half the picture's width, so a
// picture that is not a card of this shape is drawn whole.
std::optional<SquareCrop> FaceSquare(const std::uint8_t* pixels, int width, int height, std::size_t rowPitch);

} // namespace sfve::adapter
