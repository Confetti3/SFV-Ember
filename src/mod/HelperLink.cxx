#include "mod/HelperLink.hxx"

#include "core/GameAdapter.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"

#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

#include <nlohmann/json.hpp>

namespace sfve::mod {

namespace {

// Bound into the room's invitations: a different build cannot join.
constexpr const char* kBuild = "sfv-ember/13173604/0.1";
constexpr unsigned kPumpWaitMs = 20;
constexpr unsigned kConnectTimeoutMs = 10000;

void Random(uint8_t* bytes, size_t size)
{
	BCryptGenRandom(nullptr, bytes, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

// The helper sends a room message as a raw-body frame (its JSON header, then
// the payload's bytes), so the text is not escaped; MatchLink reads events as
// plain JSON, with the body back in the header's "payload" field.
std::string EventJson(const std::string& frame)
{
	namespace hf = sf4e::platform::helper_frame;
	size_t headerSize = 0;
	if (hf::SplitPayload(frame, headerSize) != hf::PayloadKind::Raw) return frame;
	nlohmann::json event = nlohmann::json::parse(frame.substr(hf::RawPrefix, headerSize), nullptr, false);
	if (!event.is_object() || event.value("type", std::string()) != "message") return std::string();
	event["payload"] = frame.substr(hf::RawPrefix + headerSize);
	return event.dump();
}

// A loopback UDP port free right now, for GGPO to bind when the battle starts.
uint16_t FreeLoopbackPort()
{
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) return 0;
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int length = sizeof(addr);
	uint16_t port = 0;
	if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
		getsockname(s, reinterpret_cast<sockaddr*>(&addr), &length) == 0) {
		port = ntohs(addr.sin_port);
	}
	closesocket(s);
	return port;
}

} // namespace

HelperLink::HelperLink(int inputDelay)
	: inputDelay_(inputDelay),
	  link_(kBuild, GgpoMaximumPacket(sizeof(PackedInput)), MatchLink::Dependencies{Random, FreeLoopbackPort})
{
}

HelperLink::~HelperLink()
{
	stop_ = true;
	if (pump_.joinable()) pump_.join();
	client_.Stop();
	process_.Stop();
}

bool HelperLink::Start()
{
	const std::wstring exe = ModuleFolder() + L"\\sf4-net.exe";
	if (!process_.Start(exe, GetCurrentProcessId())) {
		failure_ = "could not start sf4-net.exe (error " + std::to_string(process_.LastError()) + ")";
		Log("helper: %s", failure_.c_str());
		return false;
	}
	if (!client_.Start(process_.Bootstrap())) {
		failure_ = "could not connect to the helper";
		Log("helper: %s", failure_.c_str());
		return false;
	}
	const ULONGLONG deadline = GetTickCount64() + kConnectTimeoutMs;
	while (client_.State() == sf4e::platform::HelperState::Connecting && GetTickCount64() < deadline) Sleep(10);
	if (client_.State() != sf4e::platform::HelperState::Connected) {
		failure_ = "the helper did not authenticate";
		Log("helper: %s", failure_.c_str());
		return false;
	}
	Log("helper: connected (IPC version %u)", client_.HelperIpcVersion());
	pump_ = std::thread([this] { Pump(); });
	return true;
}

void HelperLink::Host()
{
	std::lock_guard lock(mutex_);
	link_.Host(inputDelay_);
	SendCommands();
}

void HelperLink::Join(const std::string& invitation)
{
	std::lock_guard lock(mutex_);
	link_.Join(invitation);
	SendCommands();
}

void HelperLink::SendCommands()
{
	for (std::string& command : link_.TakeCommands()) {
		if (!client_.Send(std::move(command))) Log("helper: a command could not be queued");
	}
}

void HelperLink::Pump()
{
	sf4e::platform::HelperMessage message;
	MatchLink::Phase logged = MatchLink::Phase::Idle;
	while (!stop_) {
		client_.WaitForIncoming(kPumpWaitMs);
		if (client_.State() == sf4e::platform::HelperState::Failed) {
			Log("helper: the connection to the helper failed");
			return;
		}
		std::lock_guard lock(mutex_);
		while (client_.TryReceive(message)) link_.OnEvent(EventJson(message.payload));
		SendCommands();
		if (link_.GetPhase() != logged) {
			logged = link_.GetPhase();
			Log("helper: match link %s%s%s", PhaseName(logged), link_.Failure().empty() ? "" : ": ",
				link_.Failure().c_str());
		}
	}
}

std::optional<MatchLink::Endpoints> HelperLink::Ready() const
{
	std::lock_guard lock(mutex_);
	if (link_.GetPhase() != MatchLink::Phase::Ready) return std::nullopt;
	return link_.GetEndpoints();
}

std::string HelperLink::Describe() const
{
	std::lock_guard lock(mutex_);
	if (!failure_.empty()) return "{\"failure\":" + JsonString(failure_) + "}";
	return link_.Describe();
}

void HelperLink::RegisterCommands()
{
	RegisterCommand("host", [this](const std::string&) {
		Host();
		return std::string("ok");
	});
	RegisterCommand("join", [this](const std::string& invitation) {
		if (invitation.empty()) return std::string("error usage: join <invitation>");
		Join(invitation);
		return std::string("ok");
	});
	RegisterStatus("link", [this] { return Describe(); });
}

} // namespace sfve::mod
