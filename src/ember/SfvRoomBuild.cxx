#include "ember/SfvRoomBuild.hxx"

namespace sfve::room {

namespace {

constexpr char kLayoutPart[] = "/layout-";
constexpr size_t kLayoutIdSize = 16;

} // namespace

std::string RoomBuildId(const std::string& pickBuild, const std::string& layoutId)
{
	return pickBuild + kLayoutPart + layoutId;
}

std::string LayoutIdOf(const std::string& roomBuild)
{
	const size_t part = roomBuild.rfind(kLayoutPart);
	if (part == std::string::npos) return {};
	const std::string id = roomBuild.substr(part + sizeof(kLayoutPart) - 1);
	if (id.size() != kLayoutIdSize || id.find_first_not_of("0123456789abcdef") != std::string::npos) return {};
	return id;
}

} // namespace sfve::room
