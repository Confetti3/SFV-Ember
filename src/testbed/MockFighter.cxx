#include "testbed/MockFighter.hxx"

#include <stddef.h>

namespace sfve::testbed {

namespace {

constexpr int32_t kGround = 0;
constexpr int32_t kStageHalfWidth = 30000;
constexpr int32_t kWalkSpeed = 300;
constexpr int32_t kJumpSpeed = 1200;
constexpr int32_t kGravity = 80;
constexpr int32_t kReach = 2500;
constexpr int32_t kPushbox = 1500;

uint32_t NextRandom(uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	return state >> 8;
}

} // namespace

MockFighter::MockFighter(unsigned bugs)
	: bugs_(bugs), heapMarker_(std::make_unique<int>(0))
{
	world_.rng = 0x5eed1234u;
	world_.roundTimer = 99 * 60;
	world_.koAt = -1;
	world_.skippedAt = -1;
	for (int p = 0; p < kPlayers; p++) {
		Fighter& f = world_.fighters[p];
		f = {};
		f.x = p == 0 ? -8000 : 8000;
		f.y = kGround;
		f.health = 1000;
		f.facing = p == 0 ? 1 : -1;
	}
	world_.owner = heapMarker_.get();
	// Like a clock read when the process starts: two games never share one.
	static int32_t constructed = 0;
	world_.clock = ++constructed;
	Build();
}

void MockFighter::Build()
{
	layout_.Clear();
	const size_t world = layout_.Add("world", &world_, sizeof(world_));
	if (!(bugs_ & kBugUnmaskedPointer)) layout_.IgnoreWord(world, offsetof(World, owner));
	if (!(bugs_ & kBugUnmaskedClock)) layout_.IgnoreForPeers(world, offsetof(World, clock));
	layout_.Add("presentation", &presentation_, sizeof(presentation_), false);
	if (!(bugs_ & kBugFlowOutsideLayout)) layout_.Add("flow", &flowKoFrame_, sizeof(flowKoFrame_));
}

std::string MockFighter::BuildId() const
{
	return "mockfighter-1";
}

void MockFighter::PoseReadsEvery(int every, int run, bool needsPlacement)
{
	poseEvery_ = every;
	poseRun_ = run < 1 ? 1 : run;
	poseNeedsPlacement_ = needsPlacement;
	placed_ = PoseOf(world_);
	placedSinceRestore_ = true;
}

uint32_t MockFighter::PoseOf(const World& world)
{
	uint32_t h = 2166136261u;
	const auto mix = [&](int32_t v) {
		for (int i = 0; i < 4; i++) h = (h ^ (uint8_t)(v >> (8 * i))) * 16777619u;
	};
	for (const Fighter& f : world.fighters) {
		mix(f.x);
		mix(f.y);
		mix(f.attackFrames);
		mix(f.hitstun);
		mix(f.facing);
	}
	return h;
}

void MockFighter::ReadPose()
{
	missed_ = false;
	if (poseEvery_ <= 0 || world_.frame <= 0 || world_.frame % poseEvery_ >= poseRun_ || !InputsMatter()) return;
	const uint32_t pose = PoseOf(world_);
	uint32_t value = 0;
	if (fresh_) {
		// What the engine drew: the pose of this state.
		value = drawn_;
		posesSeen_.insert(pose);
	} else if (posesSeen_.count(pose) && (!poseNeedsPlacement_ || placedSinceRestore_)) {
		value = pose;
	} else {
		// The live value: the pose last drawn, another state's.
		value = drawn_;
		missed_ = true;
		poseMisses_++;
	}
	world_.rng ^= value;
}

void MockFighter::NotePoseHold(const pose_hold::Event& event)
{
	switch (event.kind) {
	case pose_hold::Event::Kind::Pending: posePending_++; break;
	case pose_hold::Event::Kind::Cancelled: poseCancelled_++; break;
	case pose_hold::Event::Kind::Held:
		poseHolds_++;
		awaitingHold_ = event.frame;
		break;
	case pose_hold::Event::Kind::Refused:
		poseRefused_++;
		refusedBy_[(int)event.why]++;
		break;
	}
}

void MockFighter::Advance(const FrameInputs& inputs, bool resimulating)
{
	if (awaitingHold_ >= 0) {
		if (fresh_ && world_.frame == awaitingHold_) freshAfterHold_++;
		else notFreshAfterHold_++;
		awaitingHold_ = -1;
	}
	ReadPose();
	if (resimulating) resimulated_++;
	if (resimulating && !lastWasResimulation_) rollbackTargets_.push_back(world_.frame);
	lastWasResimulation_ = resimulating;
	callsSinceStart_++;
	// A skip of the ignored frames (SkipEvery) ends them from the next frame.
	if (!InputsMatter() && ((inputs.p[0] | inputs.p[1]) & kSkip)) world_.skippedAt = world_.frame + 1;

	for (int p = 0; p < kPlayers; p++) {
		Fighter& f = world_.fighters[p];
		const PackedInput in = inputs.p[p];
		if (f.hitstun > 0) {
			f.hitstun--;
			f.vx = 0;
		} else {
			f.vx = ((in & kForward ? kWalkSpeed : 0) - (in & kBack ? kWalkSpeed : 0)) * f.facing;
			if ((in & kUp) && f.y == kGround) f.vy = kJumpSpeed;
			if ((in & kPunch) && f.attackFrames == 0) f.attackFrames = 12;
		}
		f.x += f.vx;
		if (f.x > kStageHalfWidth) f.x = kStageHalfWidth;
		if (f.x < -kStageHalfWidth) f.x = -kStageHalfWidth;
		f.y += f.vy;
		if (f.y > kGround) {
			f.vy -= kGravity;
		} else {
			f.y = kGround;
			f.vy = 0;
		}
	}

	// Grounded fighters cannot walk through each other.
	Fighter& f0 = world_.fighters[0];
	Fighter& f1 = world_.fighters[1];
	const int32_t gap = f1.x - f0.x;
	if (f0.y == kGround && f1.y == kGround && gap < kPushbox && gap > -kPushbox) {
		const int32_t center = f0.x + gap / 2;
		const int32_t half = gap >= 0 ? kPushbox / 2 : -kPushbox / 2;
		f0.x = center - half;
		f1.x = center + half;
	}

	for (int p = 0; p < kPlayers; p++) {
		Fighter& attacker = world_.fighters[p];
		Fighter& defender = world_.fighters[1 - p];
		attacker.facing = defender.x >= attacker.x ? 1 : -1;
		if (attacker.attackFrames == 0) continue;
		attacker.attackFrames--;
		const int32_t distance = defender.x > attacker.x ? defender.x - attacker.x : attacker.x - defender.x;
		if (attacker.attackFrames == 8 && distance < kReach && defender.hitstun == 0) {
			int32_t damage = 40 + (int32_t)(NextRandom(world_.rng) % 20);
			if (bugs_ & kBugStateOutsideLayout) damage += comboCounter_++ * 5;
			if (bugs_ & kBugReadsNonInput) damage += callsSinceStart_ % 3;
			defender.health = defender.health > damage ? defender.health - damage : 0;
			defender.hitstun = 18;
		}
	}

	if (desyncFrom_ >= 0 && world_.frame >= desyncFrom_ && world_.fighters[0].health > 0) world_.fighters[0].health--;
	if (world_.roundTimer > 0) world_.roundTimer--;
	world_.frame++;

	if (world_.koAt < 0 && (world_.fighters[0].health == 0 || world_.fighters[1].health == 0)) {
		world_.koAt = world_.frame;
	}
	// The round flow reacts to the battle's KO a little later, like SFV's.
	if (flowKoFrame_ < 0 && world_.koAt >= 0 && world_.frame - world_.koAt >= 2) flowKoFrame_ = world_.koAt;
	presentation_.animationClock += 1.0f / 60.0f;
	// The update's last phase places the models for the state it leaves; the
	// engine shows them after its next frame.
	placed_ = PoseOf(world_);
	fresh_ = false;
	placedSinceRestore_ = true;
}

} // namespace sfve::testbed
