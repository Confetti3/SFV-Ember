#include "ember/SfvMeterSample.hxx"

namespace sfve::adapter {

namespace {

using sf4e::training::ActorStatus;
using sf4e::training::BoundaryProvenance;
using sf4e::training::FighterSample;

// The header names an attack: frames first up to (not including) last, inside
// the script (kSetScript treats anything else as none).
bool HasAttack(const mod::ScriptHeader& header)
{
	return header.first >= 0 && header.last > header.first && header.last <= header.total;
}

} // namespace

std::optional<unsigned> MeterStatus(int stateKind, int posture)
{
	switch (static_cast<SfvState>(stateKind)) {
	case SfvState::Stand: return ActorStatus::Stand;
	case SfvState::Crouch: return ActorStatus::Crouch;
	case SfvState::PreJump: return ActorStatus::StandToJump;
	case SfvState::Jump: return ActorStatus::Jump;
	case SfvState::Landing: return ActorStatus::JumpToStand;
	case SfvState::WalkForward: return ActorStatus::Forward;
	case SfvState::WalkBack: return ActorStatus::Backward;
	case SfvState::DashForward: return ActorStatus::FrontDash;
	case SfvState::DashBack: return ActorStatus::BackDash;
	// One guard state stands or crouches as its script's posture does.
	case SfvState::Guard: return posture == 1 ? ActorStatus::GuardCrouch : ActorStatus::GuardStand;
	case SfvState::StandToCrouch: return ActorStatus::StandToCrouch;
	case SfvState::CrouchToStand: return ActorStatus::CrouchToStand;
	case SfvState::TurnStand: return ActorStatus::TurnStand;
	case SfvState::TurnCrouch: return ActorStatus::TurnCrouch;
	case SfvState::TurnWalk: return ActorStatus::TurnWalk;
	// Normals, specials, throws and V-skills alike.
	case SfvState::Attack: return ActorStatus::Skill;
	case SfvState::Hit: return ActorStatus::Damage;
	case SfvState::GuardHit: return ActorStatus::DamageGuard;
	// Knocked into the air: a juggle's hit or a launcher's.
	case SfvState::LaunchHit: return ActorStatus::DamageBlow;
	case SfvState::Down: case SfvState::LyingDown: return ActorStatus::Down;
	case SfvState::Rise: return ActorStatus::Rise;
	// Thrown: the thrower's script holds this fighter (the meter's sequence).
	case SfvState::Held: return ActorStatus::Sequence;
	}
	return std::nullopt;
}

int HeaderPhase(const mod::ScriptHeader& header, float frame, bool attackedBefore)
{
	if (!HasAttack(header)) return -1;
	if (frame < static_cast<float>(header.first)) return attackedBefore ? 2 : 0;
	return frame < static_cast<float>(header.last) ? 1 : 3;
}

FighterSample MeterSample(const mod::FighterFrame& fighter, MeterMove& move)
{
	FighterSample sample;
	if (!fighter.valid) {
		move = {};
		return sample;
	}
	const std::optional<unsigned> status = MeterStatus(fighter.stateKind, fighter.posture);
	// A kind section 39 does not name is drawn as unknown (status ~0u), which
	// the meter also takes as a frame it cannot measure.
	sample.valid = true;
	if (status) sample.status = *status;
	sample.action = fighter.script;
	sample.actionFrame = fighter.scriptFrame;
	sample.posture = fighter.posture;
	sample.timeScale = fighter.timeScale;
	sample.untilActionable = fighter.untilActionable;
	sample.basicActionInhibited = fighter.untilActionable > 0;
	sample.hitStop = fighter.hitStop;
	sample.health = fighter.vital;
	// What the fighter has lost of its vital, which every hit raises (and
	// Training's refill lowers again, which the meter does not take as a hit).
	sample.comboDamage = fighter.vitalMax - fighter.vital;
	const mod::ScriptHeader& header = fighter.header;
	if (header.total > 0) {
		sample.totalFrames = header.total;
		if (header.interrupt > 0 && header.interrupt <= header.total) sample.interruptibleFrame = header.interrupt;
	}
	if (HasAttack(header)) {
		sample.firstActiveFrame = header.first;
		sample.lastActiveFrame = header.last;
		sample.boundaryProvenance = BoundaryProvenance::BacActionHeader;
	}
	if (sample.status != ActorStatus::Skill) {
		move = {};
		return sample;
	}
	// The move an input started: its follow-up scripts keep it (a Shoryuken's
	// landing), the next input's move does not. No attack is action 0, the stand.
	sample.commandMove = static_cast<std::uintptr_t>(fighter.startedAction);
	if (fighter.startedAction != move.action) move = {fighter.startedAction, false};
	int phase = HeaderPhase(header, fighter.scriptFrame, move.attacked);
	// A script with no attack of its own: its projectile coming out is the
	// move's one active frame; before any attack it is startup, after one
	// recovery.
	if (phase < 0) phase = fighter.projectileOut ? 1 : move.attacked ? 3 : 0;
	sample.hitPhase = phase;
	move.attacked = move.attacked || phase == 1;
	return sample;
}

} // namespace sfve::adapter
