// The watched game's picture on SFV (overlay/WatchedFrame,
// docs/design/WATCH_PICTURE.md), checked without a device: when its two
// textures are made again, whether the back buffer is copied or resolved,
// that nothing is taken for an empty size, and that the overlay gives no
// picture before it draws on the game's swap chain.

#include <doctest/doctest.h>

#include "overlay/Overlay.hxx"
#include "overlay/WatchedFrame.hxx"

#include <dxgiformat.h>

using sfve::overlay::PicturePlan;
using sfve::overlay::PlanPicture;
using sfve::overlay::TextureShape;

namespace {
constexpr unsigned kBgra = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr unsigned kPicture = DXGI_FORMAT_B8G8R8X8_UNORM;
const TextureShape kBack{1920, 1080, kBgra, 1};
} // namespace

TEST_CASE("the first picture makes both textures and copies the frame")
{
	const PicturePlan plan = PlanPicture(kBack, {}, {}, 1190, 669);
	CHECK(plan.take);
	CHECK(plan.remakeFrame);
	CHECK(plan.remakePicture);
	CHECK_FALSE(plan.resolve);
}

TEST_CASE("the next pictures of the same frame and size make nothing again")
{
	const PicturePlan plan = PlanPicture(kBack, kBack, {1190, 669, kPicture, 1}, 1190, 669);
	CHECK(plan.take);
	CHECK_FALSE(plan.remakeFrame);
	CHECK_FALSE(plan.remakePicture);
}

TEST_CASE("a back buffer of another size or format makes the copy again, the picture only for another size asked")
{
	const TextureShape picture{1190, 669, kPicture, 1};
	const PicturePlan resized = PlanPicture({2560, 1440, kBgra, 1}, kBack, picture, 1190, 669);
	CHECK(resized.remakeFrame);
	CHECK_FALSE(resized.remakePicture);
	const PicturePlan reformatted = PlanPicture({1920, 1080, DXGI_FORMAT_R10G10B10A2_UNORM, 1}, kBack, picture, 1190, 669);
	CHECK(reformatted.remakeFrame);
	const PicturePlan larger = PlanPicture(kBack, kBack, picture, 1587, 892);
	CHECK_FALSE(larger.remakeFrame);
	CHECK(larger.remakePicture);
}

TEST_CASE("a multisampled back buffer is resolved into a single-sampled copy")
{
	const TextureShape multisampled{1920, 1080, kBgra, 4};
	const PicturePlan first = PlanPicture(multisampled, {}, {}, 1190, 669);
	CHECK(first.resolve);
	CHECK(first.remakeFrame);
	// The copy made for it has one sample: the next frame keeps it.
	const PicturePlan next = PlanPicture(multisampled, kBack, {1190, 669, kPicture, 1}, 1190, 669);
	CHECK(next.resolve);
	CHECK_FALSE(next.remakeFrame);
}

TEST_CASE("nothing is taken for an empty back buffer or size asked")
{
	CHECK_FALSE(PlanPicture({}, {}, {}, 1190, 669).take);
	CHECK_FALSE(PlanPicture(kBack, {}, {}, 0, 669).take);
	CHECK_FALSE(PlanPicture(kBack, {}, {}, 1190, 0).take);
}

TEST_CASE("the overlay gives no picture before it draws on the game's swap chain")
{
	CHECK_FALSE(sfve::overlay::WatchedPicture(1190, 669));
}
