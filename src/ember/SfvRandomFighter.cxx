#include "ember/SfvRandomFighter.hxx"

namespace sfve::adapter {

// Stream C fills these. Until then Random is never on and nothing is rolled.

void NoteRandomFighter(bool)
{
}

void FollowRandomFighter(const sf4e::NetplayFacade::RuntimeSnapshot&)
{
}

void RandomFighterPlayed()
{
}

} // namespace sfve::adapter
