// SFV's boxes in the hitbox viewer's terms (ember/SfvBoxes) on records and a
// camera recorded in game: the maintainer's desync state at checkpoint 3360
// of 2026-11-11 (docs/design/beta8/desync.md; Z33 against Z37 on beta.7,
// build 13173604), whose action parts' regions hold both blocks of every
// part and whose CameraUnit+0x100 run holds the camera. Each recorded record
// is put back where it was, at its address, with the list entries that
// pointed at it, and nothing else of the 12 KB around it: what the viewer
// reads of a block is exactly that (SfvBoxOffsets.hxx). The numbers each
// check expects are the game's own: the rectangles the game placed and the
// screen the camera shows them on (docs/design/beta8/hitboxes.md).

#include <doctest/doctest.h>

#include "ember/SfvBoxes.hxx"

#include "training/HitBoxes.hxx"

#include <cmath>
#include <cstring>
#include <vector>

using sfve::adapter::AppendBoxes;
using sfve::adapter::AttackBoxKind;
using sfve::adapter::BoxTally;
using sfve::adapter::CameraMatrices;
using sfve::adapter::SfvBoxView;
using sfve::mod::BoxBlocks;
using sfve::mod::BoxFrame;
using sfve::mod::CameraFrame;
namespace boxes = sfve::mod::sfv::boxes;
namespace training = sf4e::training;
using training::BoxKind;

namespace {

// The image base of the recorded process: its records' shape vtable less the
// rectangle's rva (every listed record's was that one).
constexpr uintptr_t kImageBase = 0x7FF7468B0000;
constexpr uintptr_t kRectShape = kImageBase + boxes::kRectShapeVtable;

float Bits(uint32_t bits)
{
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

// A recorded record: the list it was in (an attack type, or a body group and
// list), its place in that list, its index among the block's records, its
// rectangle's bits (left, bottom, width, height) and its +48 and +52.
struct Listed {
	int list, group;
	uint32_t slot, record;
	uint32_t rect[4];
	int type, groupField;
};

struct RecordedPart {
	int side;
	bool projectile;
	uintptr_t part;
	uint64_t attackRecords, bodyRecords;
	std::vector<Listed> attack, body;
};

template <size_t N>
void Put(std::array<uint8_t, N>& block, size_t offset, const void* value, size_t size)
{
	REQUIRE(offset + size <= N);
	std::memcpy(block.data() + offset, value, size);
}

template <size_t N>
void PutRecord(std::array<uint8_t, N>& block, uintptr_t blockAt, uint32_t list, const Listed& listed, uintptr_t shape)
{
	const size_t record = boxes::kRecords + size_t(listed.record) * boxes::kRecordSize;
	Put(block, record + boxes::kRecordShape, &shape, 8);
	Put(block, record + boxes::kRecordRect, listed.rect, 16);
	Put(block, record + boxes::kRecordType, &listed.type, 4);
	Put(block, record + boxes::kRecordGroup, &listed.groupField, 4);
	const uint64_t entry = blockAt + record;
	Put(block, list + 8 * size_t(listed.slot), &entry, 8);
	uint64_t count = 0;
	std::memcpy(&count, block.data() + list + boxes::kListCount, 8);
	if (count < listed.slot + 1) count = listed.slot + 1;
	Put(block, list + boxes::kListCount, &count, 8);
}

BoxBlocks Blocks(const RecordedPart& recorded, uintptr_t shape = kRectShape)
{
	BoxBlocks blocks;
	blocks.side = recorded.side;
	blocks.projectile = recorded.projectile;
	blocks.attackAt = recorded.part + boxes::kAttackBlock;
	blocks.bodyAt = recorded.part + boxes::kBodyBlock;
	Put(blocks.attack, boxes::kRecordCount, &recorded.attackRecords, 8);
	Put(blocks.body, boxes::kRecordCount, &recorded.bodyRecords, 8);
	for (const Listed& listed : recorded.attack)
		PutRecord(blocks.attack, blocks.attackAt, boxes::kAttackLists + boxes::kListSize * uint32_t(listed.list), listed, shape);
	for (const Listed& listed : recorded.body)
		PutRecord(blocks.body, blocks.bodyAt,
			boxes::kBodyLists + boxes::kBodyGroupSize * uint32_t(listed.group) + boxes::kListSize * uint32_t(listed.list), listed, shape);
	return blocks;
}

// Checkpoint 3360: P1 (Z33) at x -7.02, P2 (Z37) at -2.36, P1's fireball in
// the air with its proximity box ahead of it, and a throw of P1's on the
// floor between them.
std::vector<RecordedPart> Checkpoint3360()
{
	RecordedPart p1{0, false, 0x25A35B84000, 0, 4, {}, {}};
	for (int list = 0; list < 2; list++) {
		p1.body.push_back({list, 0, 0, 0, {0xC0EBE720, 0x3FB9999A, 0x3F333333, 0x3ECCCCCD}, 3, 0});
		p1.body.push_back({list, 0, 1, 2, {0xC0EBE720, 0x00000000, 0x3F333333, 0x3F19999A}, 3, 0});
		p1.body.push_back({list, 0, 2, 1, {0xC0EBE720, 0x3F19999A, 0x3F333333, 0x3F59999A}, 3, 0});
	}
	p1.body.push_back({2, 0, 0, 3, {0xC0E8B3ED, 0x00000000, 0x3F000000, 0x3FC00000}, 4, 0});
	RecordedPart p2{1, false, 0x259FC9EC000, 0, 4, {}, {}};
	for (int list = 0; list < 2; list++) {
		p2.body.push_back({list, 0, 0, 0, {0xC023E694, 0x3FC66666, 0x3ECCCCCD, 0x3E99999A}, 3, 0});
		p2.body.push_back({list, 0, 1, 2, {0xC02D802D, 0x00000000, 0x3F333333, 0x3F266666}, 3, 0});
		p2.body.push_back({list, 0, 2, 1, {0xC02D802D, 0x3F266666, 0x3F333333, 0x3F733333}, 3, 0});
	}
	p2.body.push_back({2, 0, 0, 3, {0xC02719C7, 0x00000000, 0x3F000000, 0x3FC00000}, 4, 0});
	RecordedPart fireball{0, true, 0x259E8E84000, 2, 0, {}, {}};
	fireball.attack.push_back({1, 0, 0, 1, {0xC0D1CA8F, 0x400FDF3B, 0x3ECCCCCD, 0x3ECCCCCD}, 1, 2});
	fireball.attack.push_back({4, 0, 0, 0, {0xC0CB6429, 0x401CAC08, 0x40000000, 0x3E99999A}, 4, 1});
	RecordedPart thrown{0, true, 0x25A5CB54000, 1, 0, {}, {}};
	thrown.attack.push_back({6, 0, 0, 0, {0xC090DEA7, 0x00000000, 0x3FA66666, 0x3FC00000}, 6, 99});
	return {p1, p2, fireball, thrown};
}

// The camera at checkpoint 3360 (CameraUnit+0x170, +0x17C, +0x194), with the
// fight's frame the world's (the target lies on the fight's plane, z 0).
CameraFrame Camera3360()
{
	CameraFrame camera;
	camera.valid = true;
	camera.target = {Bits(0xC3F30906), Bits(0x80000000), Bits(0x4313B3F9)};
	camera.eye = {Bits(0xC3F30906), Bits(0x43ED7EC1), Bits(0x42EF67F0)};
	camera.fieldOfView = Bits(0x42820050);
	camera.fightToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	return camera;
}

BoxFrame Frame3360()
{
	BoxFrame frame;
	frame.rectShape = kRectShape;
	for (const RecordedPart& part : Checkpoint3360()) frame.parts.push_back(Blocks(part));
	frame.camera = Camera3360();
	return frame;
}

// A point of the fight's plane on a 1920x1080 screen.
struct Point {
	float x, y;
	bool seen;
};
Point OnScreen(const training::BoxView& view, float x, float y)
{
	Point point{};
	point.seen = training::Project(view.view, view.projection, x, y, 1920, 1080, point.x, point.y);
	return point;
}

bool Near(Point point, float x, float y)
{
	return point.seen && std::fabs(point.x - x) < 0.5f && std::fabs(point.y - y) < 0.5f;
}

} // namespace

TEST_CASE("an attack box's type is drawn as the game tests it")
{
	CHECK(AttackBoxKind(0) == BoxKind::Hit);
	CHECK(AttackBoxKind(1) == BoxKind::Hit);
	CHECK(AttackBoxKind(2) == BoxKind::Hit);
	CHECK(AttackBoxKind(4) == BoxKind::Proximity);
	CHECK(AttackBoxKind(6) == BoxKind::Throw);
	// No attack's reach: never found tested, and the catch.
	CHECK_FALSE(AttackBoxKind(3).has_value());
	CHECK_FALSE(AttackBoxKind(5).has_value());
	CHECK_FALSE(AttackBoxKind(7).has_value());
	CHECK_FALSE(AttackBoxKind(-1).has_value());
}

TEST_CASE("the recorded blocks give each fighter's body boxes once, and the projectiles' attack, proximity and throw boxes")
{
	BoxTally tally;
	const training::BoxView view = SfvBoxView(Frame3360(), tally);
	CHECK(tally.drawn == 11);
	CHECK(tally.stray == 0);
	CHECK(tally.otherShapes == 0);
	CHECK(tally.empty == 0);
	CHECK(tally.undrawn == 0);
	REQUIRE(view.boxes.size() == 11u);
	// P1's four body boxes: head, legs, body (each listed twice), then the
	// tall one, in the order the game's lists hold them; left, bottom, width
	// and height as the game placed them.
	const auto box = [&](size_t i) { return view.boxes[i]; };
	for (size_t i = 0; i < 4; i++) {
		CHECK(box(i).kind == BoxKind::Hurt);
		CHECK(box(i).side == 0);
	}
	CHECK(box(0).x[0] == Bits(0xC0EBE720));
	CHECK(box(0).y[2] == Bits(0x3FB9999A));
	CHECK(box(0).y[0] == Bits(0x3FB9999A) + Bits(0x3ECCCCCD));
	CHECK(box(0).x[1] == Bits(0xC0EBE720) + Bits(0x3F333333));
	CHECK(box(1).y[2] == 0.0f);
	CHECK(box(1).y[0] == Bits(0x3F19999A));
	CHECK(box(2).y[2] == Bits(0x3F19999A));
	CHECK(box(3).x[0] == Bits(0xC0E8B3ED));
	CHECK(box(3).y[0] == 1.5f);
	for (size_t i = 4; i < 8; i++) {
		CHECK(box(i).kind == BoxKind::Hurt);
		CHECK(box(i).side == 1);
	}
	CHECK(box(4).x[0] == Bits(0xC023E694));
	// The fireball's: its hit (type 1), then its proximity box reaching 2 m
	// ahead of it (type 4); then the throw on the floor (type 6). All P1's.
	CHECK(box(8).kind == BoxKind::Hit);
	CHECK(box(8).x[0] == Bits(0xC0D1CA8F));
	CHECK(box(9).kind == BoxKind::Proximity);
	CHECK(std::fabs(box(9).x[1] - box(9).x[0] - 2.0f) < 1e-5f);
	CHECK(box(10).kind == BoxKind::Throw);
	CHECK(box(10).y[2] == 0.0f);
	for (size_t i = 8; i < 11; i++) CHECK(box(i).side == 0);
	// Each box's corners go round it: top left, top right, bottom right, bottom left.
	for (const auto& b : view.boxes) {
		CHECK(b.x[0] == b.x[3]);
		CHECK(b.x[1] == b.x[2]);
		CHECK(b.y[0] == b.y[1]);
		CHECK(b.y[2] == b.y[3]);
		CHECK(b.x[1] > b.x[0]);
		CHECK(b.y[0] > b.y[2]);
	}
}

TEST_CASE("the recorded camera shows the fight as the game drew it")
{
	BoxTally tally;
	const training::BoxView view = SfvBoxView(Frame3360(), tally);
	REQUIRE(view.valid);
	// The point the camera looks at is the screen's middle.
	CHECK(Near(OnScreen(view, Bits(0xC3F30906) / 100, Bits(0x4313B3F9) / 100), 960, 540));
	// P1's feet at the left, P2's at the right, both on the floor line just
	// above the screen's bottom (under SFV's gauges), and P1's head a little
	// above the middle; the fireball above P1's head, the throw between them.
	CHECK(Near(OnScreen(view, Bits(0xC0EBE720), 0), 149.90f, 1015.65f));
	CHECK(Near(OnScreen(view, Bits(0xC0EBE720), Bits(0x3FB9999A) + Bits(0x3ECCCCCD)), 168.35f, 422.63f));
	CHECK(Near(OnScreen(view, Bits(0xC02D802D) + Bits(0x3F333333), 0), 1879.30f, 1015.65f));
	CHECK(Near(OnScreen(view, Bits(0xC0D1CA8F), Bits(0x400FDF3B) + Bits(0x3ECCCCCD)), 430.78f, 175.09f));
	CHECK(Near(OnScreen(view, Bits(0xC090DEA7), 0), 1067.59f, 1015.65f));
}

TEST_CASE("the fight's frame in the world moves the camera's view with it")
{
	// The same fight a metre and a half along x in the world, with the
	// camera moved with it: every point lands where it did.
	BoxTally tally;
	const training::BoxView at = SfvBoxView(Frame3360(), tally);
	BoxFrame moved = Frame3360();
	moved.camera.fightToWorld[12] = 1.5f;
	moved.camera.target[0] += 150;
	moved.camera.eye[0] += 150;
	const training::BoxView there = SfvBoxView(moved, tally);
	REQUIRE(there.valid);
	for (const float x : {-7.0f, -4.5f, -2.0f})
		for (const float y : {0.0f, 1.0f, 2.0f}) {
			const Point a = OnScreen(at, x, y), b = OnScreen(there, x, y);
			CHECK(a.seen);
			CHECK(b.seen);
			CHECK(std::fabs(a.x - b.x) < 0.05f);
			CHECK(std::fabs(a.y - b.y) < 0.05f);
		}
}

TEST_CASE("a camera that cannot be one gives no view, and the boxes stay")
{
	float view[16] = {}, projection[16] = {};
	CameraFrame camera = Camera3360();
	CHECK(CameraMatrices(camera, view, projection));
	CameraFrame unread = camera;
	unread.valid = false;
	CHECK_FALSE(CameraMatrices(unread, view, projection));
	CameraFrame nowhere = camera;
	nowhere.eye = nowhere.target;
	CHECK_FALSE(CameraMatrices(nowhere, view, projection));
	CameraFrame down = camera;
	down.eye = {down.target[0], down.target[1], down.target[2] + 500};
	CHECK_FALSE(CameraMatrices(down, view, projection));
	for (const float fov : {0.0f, 180.0f, -10.0f, NAN}) {
		CameraFrame wide = camera;
		wide.fieldOfView = fov;
		CHECK_FALSE(CameraMatrices(wide, view, projection));
	}
	CameraFrame broken = camera;
	broken.fightToWorld[5] = INFINITY;
	CHECK_FALSE(CameraMatrices(broken, view, projection));
	BoxFrame frame = Frame3360();
	frame.camera = unread;
	BoxTally tally;
	const training::BoxView boxesOnly = SfvBoxView(frame, tally);
	CHECK_FALSE(boxesOnly.valid);
	CHECK(boxesOnly.boxes.size() == 11u);
}

TEST_CASE("list entries that are no record of their block, other shapes, empty rectangles and undrawn types are left out and counted")
{
	const RecordedPart thrown = Checkpoint3360()[3];
	BoxBlocks blocks = Blocks(thrown);
	// The throw's list gains entries: one into the body block, one between
	// two records, one past the block's record count, and one at a record of
	// type 5 (the catch) in list 5.
	const uint32_t list = boxes::kAttackLists + boxes::kListSize * 6;
	const uint64_t entries[] = {blocks.bodyAt + boxes::kRecords, blocks.attackAt + boxes::kRecords + 60,
		blocks.attackAt + boxes::kRecords + boxes::kRecordSize * 3};
	for (size_t i = 0; i < 3; i++) Put(blocks.attack, list + 8 * (1 + i), &entries[i], 8);
	const uint64_t four = 4;
	Put(blocks.attack, list + boxes::kListCount, &four, 8);
	Listed caught{5, 0, 0, 1, {0, 0, 0x3F800000, 0x3F800000}, 5, 0};
	const uint64_t two = 2;
	Put(blocks.attack, boxes::kRecordCount, &two, 8);
	PutRecord(blocks.attack, blocks.attackAt, boxes::kAttackLists + boxes::kListSize * 5, caught, kRectShape);
	std::vector<training::Box> out;
	BoxTally tally;
	AppendBoxes(blocks, kRectShape, out, tally);
	CHECK(out.size() == 1u);
	CHECK(tally.drawn == 1);
	CHECK(tally.stray == 3);
	CHECK(tally.undrawn == 1);
	CHECK(tally.undrawnTypes == (1u << 5));
	// A record whose shape is not the rectangle, and one with no width.
	BoxBlocks other = Blocks(thrown, kRectShape + 0x58);
	tally = {};
	out.clear();
	AppendBoxes(other, kRectShape, out, tally);
	CHECK(out.empty());
	CHECK(tally.otherShapes == 1);
	RecordedPart flat = thrown;
	flat.attack[0].rect[2] = 0;
	tally = {};
	AppendBoxes(Blocks(flat), kRectShape, out, tally);
	CHECK(out.empty());
	CHECK(tally.empty == 1);
	// A list's count past the 16 the game files reads 16 entries.
	BoxBlocks crowded = Blocks(thrown);
	const uint64_t many = 40;
	Put(crowded.attack, list + boxes::kListCount, &many, 8);
	tally = {};
	AppendBoxes(crowded, kRectShape, out, tally);
	CHECK(tally.drawn + tally.stray == 16);
}
