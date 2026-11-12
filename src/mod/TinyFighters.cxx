#include "mod/TinyFighters.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

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
// Both hooks are in (InstallTinyFighters); otherwise nothing is drawn small
// and the fight's update has nothing to undo.
std::atomic<bool> s_hooked;

// Any thread writes (a launch, a battle's close); the game thread reads.
std::atomic<bool> s_tiny[2];
// The actors the per-frame hook last scaled for a tiny side, so the transform
// hook knows them; none for a normal side. Cleared with the sides.
std::atomic<void*> s_actor[2];
// A battle update runs (BeginFightUpdate to EndFightUpdate): the fight may
// read the model, so nothing is scaled. The game thread's.
std::atomic<bool> s_fightUpdate;

// The photo booth (development builds): allowed by BoothMode's install; each
// side's size factor it set (0: none, the side is as a Tiny pick or normal
// makes it); and each side's CharaVisualPart as the per-frame hook last saw
// it in this battle (ForgetFighterScale drops them).
std::atomic<bool> s_boothAllowed;
std::atomic<float> s_boothFactor[2];
std::atomic<uint8_t*> s_boothPart[2];

void NoteScaled(const tiny::PartResult& result)
{
	if (result.side >= 0) s_actor[result.side].store(result.scaled ? result.actor : nullptr);
}

// The booth's memory of each side's part: the part's side read as
// ApplyToPart reads it, with no side to scale (nothing is called).
void RememberPart(uint8_t* part)
{
	const int side = tiny::ApplyToPart(part, TinySides{}, s_setScale).side;
	if (side >= 0) s_boothPart[side].store(part);
}

void h_VisualUpdate(uint8_t* part)
{
	o_visualUpdate(part);
	// Remembered whether or not a battle update runs: the per-frame update
	// may be the fight's own.
	if (s_boothAllowed.load()) RememberPart(part);
	const TinySides sides = TinyFightersNow();
	if (!sides.Any() || s_fightUpdate.load()) return;
	NoteScaled(tiny::ApplyToPart(part, sides, s_setScale));
}

// The side whose scaled actor `actor` is; -1 for none.
int TinyActorSide(void* actor)
{
	if (!actor) return -1;
	return actor == s_actor[0].load() ? 0 : actor == s_actor[1].load() ? 1 : -1;
}

void h_ActorSetMeshTransform(void* actor, const __m128* transform)
{
	const int side = transform && !s_fightUpdate.load() ? TinyActorSide(actor) : -1;
	const TinySides sides = TinyFightersNow();
	if (side < 0 || !sides.side[side]) return o_setTransform(actor, transform);
	alignas(16) __m128 scaled[3];
	tiny::ScaledTransform(transform, scaled, sides.factor[side]);
	o_setTransform(actor, scaled);
}

} // namespace

namespace tiny {

void NormalScale(const float wanted[3], float out[3])
{
	for (int i = 0; i < 3; i++) {
		const float w = wanted[i];
		out[i] = std::isfinite(w) && w > 0.0f ? w : 1.0f;
	}
}

void ScaledScale(const float wanted[3], float out[3], float factor)
{
	NormalScale(wanted, out);
	for (int i = 0; i < 3; i++) out[i] *= factor;
}

bool UsableFactor(float factor)
{
	return std::isfinite(factor) && factor > 0.0f;
}

namespace {

// ApplyToPart and RestoreToPart: a tiny side's mesh given the part's wanted
// scale, times the side's factor (`scaled`) or as it is.
PartResult ScalePart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale, bool scaled)
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
	if (scaled) ScaledScale(wanted, scale, sides.factor[side]);
	else NormalScale(wanted, scale);
	setScale(const_cast<uint8_t*>(mesh), scale);
	result.actor = const_cast<uint8_t*>(actor);
	result.scaled = true;
	return result;
}

} // namespace

PartResult ApplyToPart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale)
{
	return ScalePart(part, sides, setScale, true);
}

PartResult RestoreToPart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale)
{
	return ScalePart(part, sides, setScale, false);
}

bool ScaleBothSides(const uint8_t* const parts[2], const float factors[2], SetScaleFn setScale, PartResult results[2])
{
	results[0] = results[1] = PartResult{};
	if (!UsableFactor(factors[0]) || !UsableFactor(factors[1])) return false;
	TinySides sides;
	for (int i = 0; i < 2; i++) {
		sides.side[i] = true;
		sides.factor[i] = factors[i];
	}
	bool both = true;
	for (int i = 0; i < 2; i++) {
		results[i] = ApplyToPart(parts[i], sides, setScale);
		// A part remembered for one side that now reads as the other's is not
		// counted for the side it was remembered for.
		both = both && results[i].scaled && results[i].side == i;
	}
	return both;
}

void ScaledTransform(const __m128 in[3], __m128 out[3], float factor)
{
	out[0] = _mm_loadu_ps(reinterpret_cast<const float*>(in + 0));
	out[1] = _mm_loadu_ps(reinterpret_cast<const float*>(in + 1));
	alignas(16) float scale[4];
	_mm_store_ps(scale, _mm_loadu_ps(reinterpret_cast<const float*>(in + 2)));
	float scaled[3];
	ScaledScale(scale, scaled, factor);
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
	// A side the booth scaled keeps its actor: its scale stays until the
	// booth forgets it.
	for (int i = 0; i < 2; i++) {
		if (s_boothFactor[i].load() <= 0.0f) s_actor[i].store(nullptr);
	}
}

TinySides TinyFightersNow()
{
	TinySides sides;
	for (int i = 0; i < 2; i++) {
		const float booth = s_boothFactor[i].load();
		sides.side[i] = s_tiny[i].load() || booth > 0.0f;
		sides.factor[i] = booth > 0.0f ? booth : kTinyScale;
	}
	return sides;
}

bool ApplyFighterScaleNow(float p1, float p2)
{
	const float factors[2] = {p1, p2};
	if (!tiny::UsableFactor(p1) || !tiny::UsableFactor(p2)) return false;
	if (!s_boothAllowed.load() || !s_hooked.load()) return false;
	if (!GameThreadId() || GetCurrentThreadId() != GameThreadId()) {
		Log("tiny: the booth's scale was asked off the game thread; refused");
		return false;
	}
	const uint8_t* const parts[2] = {s_boothPart[0].load(), s_boothPart[1].load()};
	if (!parts[0] || !parts[1]) return false;
	// The factors first: the hooks keep them from the next update on, also
	// for a side whose part below could not be scaled now.
	s_boothFactor[0].store(p1);
	s_boothFactor[1].store(p2);
	tiny::PartResult results[2];
	const bool both = tiny::ScaleBothSides(parts, factors, s_setScale, results);
	for (const tiny::PartResult& result : results) NoteScaled(result);
	Log("tiny: booth scale P1 %g, P2 %g%s", p1, p2, both ? "" : " (a side's part could not be scaled now)");
	return both;
}

bool SetFightersHiddenNow(bool p1, bool p2)
{
	if (!s_boothAllowed.load() || !s_hooked.load()) return false;
	if (!GameThreadId() || GetCurrentThreadId() != GameThreadId()) {
		Log("tiny: the booth's hide was asked off the game thread; refused");
		return false;
	}
	uint8_t* actors[2] = {};
	for (int i = 0; i < 2; i++) {
		const uint8_t* part = s_boothPart[i].load();
		// The part's side is read again: a part remembered for a side is that side's.
		if (!part || tiny::ApplyToPart(part, TinySides{}, s_setScale).side != i) return false;
		if (!TryRead(part + visual::kActor, &actors[i]) || !actors[i]) return false;
		uintptr_t vtable = 0;
		if (!TryRead(actors[i], &vtable) || !vtable) return false;
	}
	using SetHiddenFn = void (*)(uint8_t* actor, bool hidden);
	const bool hidden[2] = {p1, p2};
	for (int i = 0; i < 2; i++) {
		const auto set = *reinterpret_cast<SetHiddenFn*>(*reinterpret_cast<uintptr_t*>(actors[i]) + visual::kActorSetHiddenInGame);
		set(actors[i], hidden[i]);
	}
	Log("tiny: booth hide P1 %s, P2 %s", p1 ? "hidden" : "shown", p2 ? "hidden" : "shown");
	return true;
}

void SetFighterScaleAllowed(bool on)
{
	s_boothAllowed.store(on);
}

void ForgetFighterScale()
{
	for (int i = 0; i < 2; i++) {
		const bool booth = s_boothFactor[i].exchange(0.0f) > 0.0f;
		s_boothPart[i].store(nullptr);
		if (booth && !s_tiny[i].load()) s_actor[i].store(nullptr);
	}
}

void BeginFightUpdate(uint8_t* const parts[2])
{
	const TinySides sides = TinyFightersNow();
	if (!sides.Any() || !s_hooked.load()) return;
	s_fightUpdate.store(true);
	for (int i = 0; i < 2; i++) tiny::RestoreToPart(parts[i], sides, s_setScale);
}

void EndFightUpdate(uint8_t* const parts[2])
{
	if (!s_fightUpdate.exchange(false)) return;
	const TinySides sides = TinyFightersNow();
	if (!sides.Any()) return;
	for (int i = 0; i < 2; i++) NoteScaled(tiny::ApplyToPart(parts[i], sides, s_setScale));
}

bool InstallTinyFighters(uintptr_t imageBase)
{
	s_setScale = reinterpret_cast<tiny::SetScaleFn>(imageBase + sfv::kSetWorldScale3D.rva);
	HookTransaction hooks;
	hooks.Add(imageBase, sfv::kCharaVisualUpdate, reinterpret_cast<void*>(&h_VisualUpdate), reinterpret_cast<void**>(&o_visualUpdate));
	hooks.Add(imageBase, sfv::kActorSetMeshTransform, reinterpret_cast<void*>(&h_ActorSetMeshTransform),
		reinterpret_cast<void**>(&o_setTransform));
	const bool ok = !CommitHookSet(hooks, "tiny fighters");
	s_hooked.store(ok);
	Log("tiny: %s", ok ? "the fighters' picture hooked" : "the fighters' picture NOT hooked; tiny fighters draw normal");
	return ok;
}

} // namespace sfve::mod
