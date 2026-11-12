#include "overlay/FrameGrab.hxx"

namespace sfve::overlay::grab {

// Stream A-mod fills these. Until then no grab is ever taken.

bool ParseDepthChoice(const std::string&, DepthChoice&)
{
	return false;
}

std::vector<std::uint8_t> EncodeBmp(const std::uint8_t*, int, int, int, PixelFormat)
{
	return {};
}

bool QueueGrab(const Request&, std::string& error)
{
	error = "frame grabs are not written yet";
	return false;
}

Status CurrentStatus()
{
	return {};
}

std::string DepthReport()
{
	return "{\"targets\":[]}";
}

void OnPresent(void*)
{
}

bool InstallDepthProbe()
{
	return false;
}

} // namespace sfve::overlay::grab
