#include "mod/TinyFighters.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <atomic>
#include <cmath>

namespace sfve::mod {

namespace {

namespace visual = sfv::visual;

using VisualUpdateFn = void (*)(uint8_t* part);
using SetTransformFn = void (*)(void* actor, const __m128* transform);

VisualUpdateFn o_visualUpdate;
SetTransformFn o_setTransform;
tiny::SetScaleFn s_setScale;

// Any thread writes (a launch, a battle's close); the game thread reads.
std::atomic<bool> s_tiny[2];
// The actors the per-frame hook last scaled for a tiny side, so the transform
// hook knows them; none for a normal side. Cleared with the sides.
std::atomic<void*> s_actor[2];

void h_VisualUpdate(uint8_t* part)
{
	o_visualUpdate(part);
	const TinySides sides = TinyFightersNow();
	if (!sides.Any()) return;
	const tiny::PartResult result = tiny::ApplyToPart(part, sides, s_setScale);
	if (result.side >= 0) s_actor[result.side].store(result.scaled ? result.actor : nullptr);
}

bool IsTinyActor(void* actor)
{
	return actor && (actor == s_actor[0].load() || actor == s_actor[1].load());
}

void h_ActorSetMeshTransform(void* actor, const __m128* transform)
{
	if (!transform || !IsTinyActor(actor)) return o_setTransform(actor, transform);
	alignas(16) __m128 scaled[3];
	tiny::ScaledTransform(transform, scaled);
	o_setTransform(actor, scaled);
}

} // namespace

namespace tiny {

void ScaledScale(const float wanted[3], float out[3])
{
	for (int i = 0; i < 3; i++) {
		const float w = wanted[i];
		out[i] = (std::isfinite(w) && w > 0.0f ? w : 1.0f) * kTinyScale;
	}
}

PartResult ApplyToPart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale)
{
	PartResult result;
	if (!part || !setScale) return result;
	// The owning fighter: a weak pointer, usable while its control block's use
	// count is above 0 (the game's own lock, rva 0x315870, reads the same).
	const uint8_t* object = nullptr;
	const uint8_t* control = nullptr;
	if (!TryRead(part + visual::kOwnerObject, &object) || !TryRead(part + visual::kOwnerControl, &control)) return result;
	if (!object || !control) return result;
	int uses = 0;
	if (!TryRead(control + visual::kControlUseCount, &uses) || uses <= 0) return result;
	int side = -1;
	if (!TryRead(object + sfv::fighter::kSide, &side) || side < 0 || side > 1) return result;
	result.side = side;
	if (!sides.side[side]) return result;
	const uint8_t* actor = nullptr;
	const uint8_t* mesh = nullptr;
	if (!TryRead(part + visual::kActor, &actor) || !actor) return result;
	if (!TryRead(actor + visual::kActorMesh, &mesh) || !mesh) return result;
	float wanted[3];
	for (int i = 0; i < 3; i++) {
		if (!TryRead(part + visual::kWantedScale + 4 * i, &wanted[i])) return result;
	}
	float scale[3];
	ScaledScale(wanted, scale);
	setScale(const_cast<uint8_t*>(mesh), scale);
	result.actor = const_cast<uint8_t*>(actor);
	result.scaled = true;
	return result;
}

void ScaledTransform(const __m128 in[3], __m128 out[3])
{
	out[0] = _mm_loadu_ps(reinterpret_cast<const float*>(in + 0));
	out[1] = _mm_loadu_ps(reinterpret_cast<const float*>(in + 1));
	alignas(16) float scale[4];
	_mm_store_ps(scale, _mm_loadu_ps(reinterpret_cast<const float*>(in + 2)));
	float scaled[3];
	ScaledScale(scale, scaled);
	out[2] = _mm_set_ps(scale[3], scaled[2], scaled[1], scaled[0]);
}

} // namespace tiny

void SetTinyFighters(bool p1, bool p2, bool announce)
{
	s_tiny[0].store(p1);
	s_tiny[1].store(p2);
	if (!p1 && !p2) {
		s_actor[0].store(nullptr);
		s_actor[1].store(nullptr);
		return;
	}
	if (announce) Log("tiny: %s", p1 && p2 ? "P1, P2" : p1 ? "P1" : "P2");
}

void ClearTinyFighters()
{
	s_tiny[0].store(false);
	s_tiny[1].store(false);
	s_actor[0].store(nullptr);
	s_actor[1].store(nullptr);
}

TinySides TinyFightersNow()
{
	TinySides sides;
	sides.side[0] = s_tiny[0].load();
	sides.side[1] = s_tiny[1].load();
	return sides;
}

bool ApplyFighterScaleNow(float, float)
{
	return false;
}

bool InstallTinyFighters(uintptr_t imageBase)
{
	s_setScale = reinterpret_cast<tiny::SetScaleFn>(imageBase + sfv::kSetWorldScale3D.rva);
	HookTransaction hooks;
	hooks.Add(imageBase, sfv::kCharaVisualUpdate, reinterpret_cast<void*>(&h_VisualUpdate), reinterpret_cast<void**>(&o_visualUpdate));
	hooks.Add(imageBase, sfv::kActorSetMeshTransform, reinterpret_cast<void*>(&h_ActorSetMeshTransform),
		reinterpret_cast<void**>(&o_setTransform));
	const bool ok = !CommitHookSet(hooks, "tiny fighters");
	Log("tiny: %s", ok ? "the fighters' picture hooked" : "the fighters' picture NOT hooked; tiny fighters draw normal");
	return ok;
}

} // namespace sfve::mod
