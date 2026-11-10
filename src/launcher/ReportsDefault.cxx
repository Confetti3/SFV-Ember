#include "launcher/ReportsDefault.hxx"

#include "netplay/SettingsStore.hxx"
#include "platform/ReportWorkflow.hxx"

#include <nlohmann/json.hpp>

namespace sfve::launcher {

bool ApplyReportsDefault(const std::wstring& settingsDirectory)
{
	if (settingsDirectory.empty()) return false;
	const sf4e::netplay::SettingsStore store(settingsDirectory);
	nlohmann::json saved;
	std::string error;
	if (!store.LoadLauncher(saved, error)) return false;
	if (saved.is_object() && saved.value(kReportsDefaultKey, false)) return true;
	if (!sf4e::reports::TurnReportsOn(settingsDirectory)) return false;
	return store.SaveLauncher({{kReportsDefaultKey, true}}, error);
}

} // namespace sfve::launcher
