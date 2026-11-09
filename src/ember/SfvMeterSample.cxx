#include "ember/SfvMeterSample.hxx"

namespace sfve::adapter {

namespace {

namespace training = sf4e::training;
using mod::sfv::fighter::StateKind;
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

std::optional<unsigned> MeterStatus(StateKind kind, int posture)
{
	switch (kind) {
	case StateKind::None: return std::nullopt;
	case StateKind::Stand: return ActorStatus::Stand;
	case StateKind::Crouch: return ActorStatus::Crouch;
	case StateKind::PreJump: return ActorStatus::StandToJump;
	case StateKind::Jump: return ActorStatus::Jump;
	case StateKind::Landing: return ActorStatus::JumpToStand;
	case StateKind::WalkForward: return ActorStatus::Forward;
	case StateKind::WalkBack: return ActorStatus::Backward;
	case StateKind::DashForward: return ActorStatus::FrontDash;
	case StateKind::DashBack: return ActorStatus::BackDash;
	// One guard state stands or crouches as its script's posture does.
	case StateKind::Guard: return posture == 1 ? ActorStatus::GuardCrouch : ActorStatus::GuardStand;
	case StateKind::StandToCrouch: return ActorStatus::StandToCrouch;
	case StateKind::CrouchToStand: return ActorStatus::CrouchToStand;
	case StateKind::TurnStand: return ActorStatus::TurnStand;
	case StateKind::TurnCrouch: return ActorStatus::TurnCrouch;
	case StateKind::TurnWalk: return ActorStatus::TurnWalk;
	// Normals, specials, throws and V-skills alike.
	case StateKind::Attack: return ActorStatus::Skill;
	case StateKind::Hit: return ActorStatus::Damage;
	case StateKind::GuardHit: return ActorStatus::DamageGuard;
	// Knocked into the air: a juggle's hit or a launcher's.
	case StateKind::LaunchHit: return ActorStatus::DamageBlow;
	case StateKind::Down: case StateKind::LyingDown: return ActorStatus::Down;
	case StateKind::Rise: return ActorStatus::Rise;
	// Thrown: the thrower's script holds this fighter (the meter's sequence).
	case StateKind::Held: return ActorStatus::Sequence;
	}
	// A kind in the table that was not seen in game.
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
	// The game's own count, and its "cannot time it" as the meter's.
	sample.untilActionable = fighter.untilActionable >= mod::sfv::fighter::kUntimed ? training::UntimedFrames : fighter.untilActionable;
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

void MeterFeed::Feed(training::FrameMeter& meter, const std::array<mod::FighterFrame, 2>& fighters, uint32_t ticks)
{
	tick_ += static_cast<int>(ticks);
	meter.Observe(tick_, {MeterSample(fighters[0], moves_[0]), MeterSample(fighters[1], moves_[1])});
}

} // namespace sfve::adapter
