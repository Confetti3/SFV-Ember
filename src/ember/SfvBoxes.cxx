#include "ember/SfvBoxes.hxx"

#include <cmath>
#include <cstring>

#include <bitset>

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;
namespace boxes = mod::sfv::boxes;
namespace camera = mod::sfv::camera;

template <typename T, size_t N>
T At(const std::array<uint8_t, N>& bytes, size_t offset)
{
	T value;
	std::memcpy(&value, bytes.data() + offset, sizeof(value));
	return value;
}

// The record a list entry points at, as its index in the block, or none when
// the entry is no record of this block (the game's lists never hold one; a
// read across a tick's change could).
template <size_t N>
std::optional<uint32_t> RecordOf(const std::array<uint8_t, N>& block, uintptr_t blockAt, uint64_t entry)
{
	const uint64_t count = At<uint64_t>(block, boxes::kRecordCount);
	const uint64_t first = blockAt + boxes::kRecords;
	if (entry < first || (entry - first) % boxes::kRecordSize) return std::nullopt;
	const uint64_t index = (entry - first) / boxes::kRecordSize;
	if (index >= count || index >= boxes::kMostRecords) return std::nullopt;
	return static_cast<uint32_t>(index);
}

// A list's entries: its count, at most the 16 the game files.
template <size_t N>
std::vector<uint64_t> Entries(const std::array<uint8_t, N>& block, uint32_t list)
{
	const uint64_t count = At<uint64_t>(block, list + boxes::kListCount);
	std::vector<uint64_t> entries;
	for (uint32_t i = 0; i < count && i < boxes::kListMost; i++) entries.push_back(At<uint64_t>(block, list + 8 * i));
	return entries;
}

// A record's rectangle as a box: left, bottom, width, height in fight units,
// its corners top left, top right, bottom right, bottom left (as SF4's).
template <size_t N>
bool RecordBox(const std::array<uint8_t, N>& block, uint32_t index, uintptr_t rectShape, training::BoxKind kind, int side,
	training::Box& box, BoxTally& tally)
{
	const size_t record = boxes::kRecords + size_t(index) * boxes::kRecordSize;
	if (At<uint64_t>(block, record + boxes::kRecordShape) != rectShape) {
		tally.otherShapes++;
		return false;
	}
	const float x = At<float>(block, record + boxes::kRecordRect), y = At<float>(block, record + boxes::kRecordRect + 4);
	const float w = At<float>(block, record + boxes::kRecordRect + 8), h = At<float>(block, record + boxes::kRecordRect + 12);
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || !(w > 0) || !(h > 0)) {
		tally.empty++;
		return false;
	}
	box = training::Box{{x, x + w, x + w, x}, {y + h, y + h, y, y}, kind, static_cast<uint8_t>(side)};
	return true;
}

// a * b, 4x4, rows first.
void Multiply(const float* a, const float* b, float* out)
{
	float result[16];
	for (int row = 0; row < 4; row++)
		for (int column = 0; column < 4; column++) {
			float sum = 0;
			for (int k = 0; k < 4; k++) sum += a[row * 4 + k] * b[k * 4 + column];
			result[row * 4 + column] = sum;
		}
	std::memcpy(out, result, sizeof(result));
}

struct Vec {
	float x, y, z;
};
Vec Sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
std::optional<Vec> Unit(Vec a)
{
	const float length = std::sqrt(Dot(a, a));
	if (!(length > 1e-4f) || !std::isfinite(length)) return std::nullopt;
	return Vec{a.x / length, a.y / length, a.z / length};
}

} // namespace

std::optional<training::BoxKind> AttackBoxKind(int type)
{
	switch (static_cast<boxes::AttackType>(type)) {
	case boxes::AttackType::Strike:
	case boxes::AttackType::Projectile:
	case boxes::AttackType::Hit2: return training::BoxKind::Hit;
	case boxes::AttackType::Proximity: return training::BoxKind::Proximity;
	case boxes::AttackType::Throw: return training::BoxKind::Throw;
	case boxes::AttackType::Unused3:
	case boxes::AttackType::Catch: break;
	}
	return std::nullopt;
}

void AppendBoxes(const mod::BoxBlocks& part, uintptr_t rectShape, std::vector<training::Box>& out, BoxTally& tally)
{
	training::Box box{};
	// The attack lists, by type: a record is in its type's list alone.
	for (int type = 0; type < boxes::kAttackTypes; type++) {
		const auto kind = AttackBoxKind(type);
		for (const uint64_t entry : Entries(part.attack, boxes::kAttackLists + boxes::kListSize * type)) {
			const auto index = RecordOf(part.attack, part.attackAt, entry);
			if (!index) {
				tally.stray++;
				continue;
			}
			if (!kind) {
				tally.undrawn++;
				tally.undrawnTypes |= 1u << type;
				continue;
			}
			if (RecordBox(part.attack, *index, rectShape, *kind, part.side, box, tally)) {
				out.push_back(box);
				tally.drawn++;
			}
		}
	}
	// The body lists, by group and by the attacks that may strike them: a
	// record is in each list its bits name, and is drawn once. Every group is
	// where a fighter can be hit (the attack detection tries groups 1 to 3
	// before group 0, rva 0x34D1B0; only group 0 was seen in the recorded
	// states), so each is a hurt box.
	std::bitset<boxes::kMostRecords> drawn;
	for (int group = 0; group < boxes::kBodyGroups; group++)
		for (int list = 0; list < boxes::kBodySubLists; list++)
			for (const uint64_t entry :
				Entries(part.body, boxes::kBodyLists + boxes::kBodyGroupSize * group + boxes::kListSize * list)) {
				const auto index = RecordOf(part.body, part.bodyAt, entry);
				if (!index) {
					tally.stray++;
					continue;
				}
				if (drawn[*index]) continue;
				drawn[*index] = true;
				if (RecordBox(part.body, *index, rectShape, training::BoxKind::Hurt, part.side, box, tally)) {
					out.push_back(box);
					tally.drawn++;
				}
			}
}

bool CameraMatrices(const mod::CameraFrame& frame, float view[16], float projection[16])
{
	if (!frame.valid) return false;
	const float fov = frame.fieldOfView;
	if (!std::isfinite(fov) || !(fov > 0) || !(fov < 180)) return false;
	for (const float value : frame.fightToWorld)
		if (!std::isfinite(value)) return false;
	const Vec eye{frame.eye[0], frame.eye[1], frame.eye[2]}, target{frame.target[0], frame.target[1], frame.target[2]};
	// The engine's camera: ahead to the target, right of it with the world's
	// up, and up again square to both (the engine's left-handed axes: x
	// ahead, y right, z up, so right = up x ahead).
	const auto ahead = Unit(Sub(target, eye));
	if (!ahead) return false;
	const auto right = Unit(Cross({0, 0, 1}, *ahead));
	if (!right) return false;
	const Vec up = Cross(*ahead, *right);
	// A point in the camera's (right, up, ahead), from the engine's.
	const float look[16] = {right->x, up.x, ahead->x, 0, right->y, up.y, ahead->y, 0, right->z, up.z, ahead->z, 0,
		-Dot(eye, *right), -Dot(eye, up), -Dot(eye, *ahead), 1};
	// The engine's units from the world's fight units, as AimCamera writes
	// the camera: (x, y, z) -> (100 x, -100 z, 100 y).
	const float s = camera::kWorldScale;
	const float units[16] = {s, 0, 0, 0, 0, 0, s, 0, 0, -s, 0, 0, 0, 0, 0, 1};
	float world[16];
	Multiply(frame.fightToWorld.data(), units, world);
	float composed[16];
	Multiply(world, look, composed);
	// The picture's: the horizontal field of view across its width, its
	// height at the game's 16:9; the depth is the distance ahead, which is
	// what training::Project divides by.
	const float across = 1.0f / std::tan(fov * 3.14159265f / 360.0f);
	const float lens[16] = {across, 0, 0, 0, 0, across * camera::kAspect, 0, 0, 0, 0, 1, 1, 0, 0, -1, 0};
	for (const float value : composed)
		if (!std::isfinite(value)) return false;
	std::memcpy(view, composed, sizeof(composed));
	std::memcpy(projection, lens, sizeof(lens));
	return true;
}

training::BoxView SfvBoxView(const mod::BoxFrame& frame, BoxTally& tally)
{
	training::BoxView view;
	for (const mod::BoxBlocks& part : frame.parts) AppendBoxes(part, frame.rectShape, view.boxes, tally);
	view.valid = CameraMatrices(frame.camera, view.view, view.projection);
	return view;
}

} // namespace sfve::adapter
