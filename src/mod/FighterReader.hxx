#pragma once

// Both fighters as SFV's battle holds them after a tick, for Ember's frame
// meter (EMBER_LIBRARY.md section 39; the offsets are SfvFighterOffsets.hxx)
// and its hitbox viewer (docs/design/beta8/hitboxes.md; SfvBoxOffsets.hxx).
// It only reads: no game field is written, and after Bind (which finds the
// fighters with the game's own lookups, which take and give back a reference)
// no game function is called, so the battle, its rollback state and its
// hashes are what they would be without it.

#include "mod/SfvBoxOffsets.hxx"
#include "mod/SfvFighterOffsets.hxx"

#include <stdint.h>

#include <array>
#include <vector>

namespace sfve::mod {

// A BAC script's attack boundaries, as its header holds them (kSetScript): the
// first attack frame, the frame after the last, the interrupt frame and the
// total; -1 where the script has none.
struct ScriptHeader {
	int first = -1, last = -1, interrupt = -1, total = -1;
};

// One fighter after a tick.
struct FighterFrame {
	bool valid = false;
	// The fighter's state: the kind of its state machine's current state.
	sfv::fighter::StateKind stateKind = sfv::fighter::StateKind::None;
	// The script the fighter plays, its frame and header, and the posture the
	// header gives (0 standing, 1 crouching, 2 airborne, 3 lying, 4 knocked
	// into the air).
	int script = -1;
	float scriptFrame = 0;
	ScriptHeader header;
	int posture = -1;
	// 1 normally, 0 while another fighter's freeze holds this one.
	float timeScale = 1;
	// The action the fighter's last state change started (an input's move);
	// its follow-up scripts keep it.
	int startedAction = -1;
	// The game's own frames until the fighter can act, hitstop included;
	// sfv::fighter::kUntimed while it cannot be timed (lying down).
	int untilActionable = 0;
	bool hitStop = false;
	float vital = 0, vitalMax = 0;
	// A projectile of this fighter's came out on this tick.
	bool projectileOut = false;
	// Training's own advantage the game keeps (kFrameAdvantage, cached): for
	// the log beside the meter's reading only.
	int gameAdvantage = 0;
};

// One action part's boxes after a tick, as the game holds them (the hitbox
// viewer's, SfvBoxOffsets.hxx): its attack and body blocks, byte for byte,
// and where they were, since their lists point at their records by address.
// A fighter's, or a projectile's that is out.
struct BoxBlocks {
	int side = -1;            // 0 P1, 1 P2
	bool projectile = false;
	uintptr_t attackAt = 0, bodyAt = 0;
	std::array<uint8_t, sfv::boxes::kAttackBlockSize> attack{};
	std::array<uint8_t, sfv::boxes::kBodyBlockSize> body{};
};

// The camera the picture is drawn through, after a tick: where it looks and
// its eye in the engine's units, its horizontal field of view in degrees, and
// the fight's frame in the world (rows first).
struct CameraFrame {
	bool valid = false;
	std::array<float, 3> target{}, eye{};
	float fieldOfView = 0;
	std::array<float, 16> fightToWorld{};
};

// Everything the hitbox viewer reads after a tick; rectShape is the address
// of the rectangle's vtable in this process (sfv::boxes::kRectShapeVtable).
struct BoxFrame {
	std::vector<BoxBlocks> parts;
	CameraFrame camera;
	uintptr_t rectShape = 0;
};

class FighterReader {
public:
	// Finds the battle's two fighters, their action and status parts and the
	// projectile units, from any unit of the battle. Game thread, once per
	// battle; false (logged) when a fighter or a part is missing.
	bool Bind(uint8_t* anyUnit, uintptr_t imageBase);
	void Forget();
	bool Bound() const { return bound_; }
	// Both fighters by side (P1 first). False when a pointer the game keeps
	// did not read; the fighters are then left invalid. Game thread, after a
	// battle tick.
	bool Read(std::array<FighterFrame, 2>& fighters);
	// Both fighters' boxes and those of every projectile out, and the
	// camera. False when a fighter's blocks did not read (the frame is then
	// empty); a projectile's that does not read is left out, and the camera
	// is invalid when it did not read. Game thread, after a battle tick.
	bool ReadBoxes(BoxFrame& frame) const;

private:
	struct Fighter {
		uint8_t* unit = nullptr;
		uint8_t* actionPart = nullptr;
		uint8_t* statusPart = nullptr;
	};
	struct Projectile {
		uint8_t* unit = nullptr;
		uint8_t* actionPart = nullptr; // null when the unit has none (its boxes are then not read)
		bool live = false;
	};
	static bool ReadBlocks(const uint8_t* actionPart, BoxBlocks& out);
	bool ReadFighter(const Fighter& fighter, FighterFrame& out) const;
	sfv::fighter::StateKind KindOf(const uint8_t* state) const;

	bool bound_ = false;
	std::array<Fighter, 2> fighters_{};
	std::vector<Projectile> projectiles_;
	// The state table's singletons by kind (sfv::fighter::kStateTable).
	std::array<uintptr_t, sfv::fighter::kStateKinds + 1> states_{};
	// The camera and the fight's frame (null when the battle has none), and
	// the rectangle's vtable here.
	uint8_t* camera_ = nullptr;
	uint8_t* field_ = nullptr;
	uintptr_t rectShape_ = 0;
};

} // namespace sfve::mod
