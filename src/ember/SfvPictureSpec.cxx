#include "ember/SfvPictureSpec.hxx"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>

namespace sfve::pictures {

namespace {

std::wstring Wide(std::string_view text)
{
	return std::wstring(text.begin(), text.end());
}

// A picture in one of a fighter's or a stage's folders; empty for no code.
Spec InFolder(std::wstring_view root, std::string_view code, std::wstring_view sub, std::wstring_view name,
	Framing framing = Framing::Whole, float aspect = 0)
{
	if (code.empty()) return {};
	return {ObjectPath(std::wstring(root) + Wide(code) + std::wstring(sub), name), framing, aspect};
}

} // namespace

std::wstring ObjectPath(std::wstring_view folder, std::wstring_view name)
{
	if (folder.empty() || name.empty()) return {};
	std::wstring path = L"/Game/";
	path.append(folder).append(L"/").append(name).append(L".").append(name);
	return path;
}

Spec FighterFace(std::string_view code)
{
	return InFolder(L"Chara/", code, L"/Hud", L"CharaSelectIconLeft", Framing::FaceSquare);
}

Spec FighterBanner(std::string_view code)
{
	return InFolder(L"Chara/", code, L"/Hud", L"CharaSelectIcon_l");
}

Spec StageCard(std::string_view code)
{
	return InFolder(L"Stage/", code, L"/UI", L"StageSelectIcon_l");
}

Spec StageBackground(std::string_view code)
{
	return InFolder(L"Stage/", code, L"/UI", L"StageSelectBG", Framing::Centre, kStageCardAspect);
}

Spec LoadoutMark(picks::LoadoutPart part, int choice)
{
	if (choice < 0 || choice >= picks::kPartChoices) return {};
	const std::wstring name =
		std::wstring(part == picks::LoadoutPart::VSkill ? L"img_Vskill_" : L"img_Vtrigger_") + std::to_wstring(choice + 1);
	return {ObjectPath(L"WinSysAsset/FighterProfileReplayLog", name)};
}

Spec ShopCostume(std::string_view code, int costume)
{
	if (code.empty() || costume < 0 || costume > 99) return {};
	wchar_t number[8];
	swprintf(number, std::size(number), L"%02d", costume);
	return {ObjectPath(L"WinSysAsset/Shop/ShopItemImage/Costume", L"shp_" + Wide(code) + L"_" + number + L"_00")};
}

Spec LeagueIcon(std::string_view name)
{
	return {ObjectPath(L"CommonAsset/LeagueIcon", Wide(name))};
}

PixelRect CentreCrop(int width, int height, float aspect)
{
	const PixelRect whole{0, 0, (std::max)(width, 0), (std::max)(height, 0)};
	if (width <= 0 || height <= 0 || !(aspect > 0)) return whole;
	if ((float)width / (float)height >= aspect) {
		const int cut = (std::min)(width, (std::max)(1, (int)std::lround(height * aspect)));
		return {(width - cut) / 2, 0, cut, height};
	}
	const int cut = (std::min)(height, (std::max)(1, (int)std::lround(width / aspect)));
	return {0, (height - cut) / 2, width, cut};
}

} // namespace sfve::pictures
