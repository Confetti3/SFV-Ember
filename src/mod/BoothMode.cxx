#include "mod/BoothMode.hxx"

namespace sfve::mod {

namespace booth {

// Stream A-mod fills the parsers. Until then nothing parses.

bool ParseSteps(std::string_view, std::vector<Step>&, std::string& error)
{
	error = "booth steps are not written yet";
	return false;
}

std::optional<Command> ParseCommand(std::string_view, std::string& error)
{
	error = "booth commands are not written yet";
	return std::nullopt;
}

} // namespace booth

// Until stream A-mod fills the mode, it plays the battle as the game does
// (PassThroughMode's behaviour) and reports nothing.

void BoothMode::Install()
{
}

void BoothMode::NewBattle(SfvAdapter&)
{
}

void BoothMode::Frame(SfvAdapter& adapter)
{
	adapter.RunOriginal();
}

std::string BoothMode::Describe() const
{
	return "null";
}

} // namespace sfve::mod
