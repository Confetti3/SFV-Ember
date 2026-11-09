#include "mod/ControlPipe.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <map>
#include <mutex>
#include <vector>

namespace sfve::mod {

namespace {

std::mutex s_mutex;
std::map<std::string, CommandHandler> s_commands;
std::vector<std::pair<std::string, StatusField>> s_status;
int s_instance = 0;

std::string Status()
{
	std::vector<std::pair<std::string, StatusField>> fields;
	{
		std::lock_guard lock(s_mutex);
		fields = s_status;
	}
	std::string json = "{\"instance\":" + std::to_string(s_instance) + ",\"pid\":" + std::to_string(GetCurrentProcessId());
	for (auto& [key, field] : fields) json += ",\"" + key + "\":" + field();
	return json + "}";
}

// The pipe's security: the process token's own default DACL (its user,
// SYSTEM, and what the token's own context needs) in place of a pipe's
// default one, which also lets Everyone and anonymous clients read. Kept for
// the process's life: the descriptor points into the token's answer.
class PipeSecurity {
public:
	PipeSecurity()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
			error_ = GetLastError();
			return;
		}
		DWORD size = 0;
		GetTokenInformation(token, TokenDefaultDacl, nullptr, 0, &size);
		token_.resize(size);
		if (!size || !GetTokenInformation(token, TokenDefaultDacl, token_.data(), size, &size)) error_ = GetLastError();
		CloseHandle(token);
		if (error_) return;
		// A null DACL would let anyone in.
		PACL dacl = reinterpret_cast<TOKEN_DEFAULT_DACL*>(token_.data())->DefaultDacl;
		if (!dacl) {
			error_ = ERROR_INVALID_ACL;
			return;
		}
		if (!InitializeSecurityDescriptor(&descriptor_, SECURITY_DESCRIPTOR_REVISION) ||
			!SetSecurityDescriptorDacl(&descriptor_, TRUE, dacl, FALSE)) {
			error_ = GetLastError();
			return;
		}
		attributes_ = {sizeof(attributes_), &descriptor_, FALSE};
	}
	PipeSecurity(const PipeSecurity&) = delete;
	PipeSecurity& operator=(const PipeSecurity&) = delete;

	// Null, for the pipe's default security, when the token gave no DACL.
	SECURITY_ATTRIBUTES* Attributes() { return error_ ? nullptr : &attributes_; }
	DWORD Error() const { return error_; }

private:
	std::vector<unsigned char> token_;
	SECURITY_DESCRIPTOR descriptor_{};
	SECURITY_ATTRIBUTES attributes_{};
	DWORD error_ = 0;
};

DWORD WINAPI PipeThread(void*)
{
	const std::wstring path = L"\\\\.\\pipe\\sfve-" + std::to_wstring(s_instance);
	static PipeSecurity security;
	if (security.Error()) {
		Log("control: %ls keeps Windows' default security: the process token gave no DACL (%lu)", path.c_str(), security.Error());
	}
	for (;;) {
		// The first instance of its name, so a process already holding the
		// name (a copy of the game still running) is found out, not shared
		// with; and this PC's clients only.
		HANDLE pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024, 64 * 1024, 0,
			security.Attributes());
		if (pipe == INVALID_HANDLE_VALUE) {
			const DWORD error = GetLastError();
			Log("control: cannot create %ls (%lu)%s", path.c_str(), error,
				error == ERROR_ACCESS_DENIED ? ": another process holds the name (a copy of the game still running?)" : "");
			return 1;
		}
		if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
			std::string buffer;
			char chunk[1024];
			DWORD n = 0;
			while (ReadFile(pipe, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
				buffer.append(chunk, n);
				size_t eol;
				while ((eol = buffer.find('\n')) != std::string::npos) {
					std::string line = buffer.substr(0, eol);
					buffer.erase(0, eol + 1);
					if (!line.empty() && line.back() == '\r') line.pop_back();
					std::string reply = Answer(line) + "\n";
					DWORD written = 0;
					WriteFile(pipe, reply.data(), (DWORD)reply.size(), &written, nullptr);
				}
			}
		}
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
	}
}

} // namespace

std::string Answer(const std::string& line, BuildKind kind)
{
	const size_t space = line.find(' ');
	const std::string name = line.substr(0, space);
	const std::string args = space == std::string::npos ? "" : line.substr(space + 1);
	if (name == "ping") return "ok pong";
	if (name == "status") return "ok " + Status();
	if (kind == BuildKind::Player) return "error '" + name + "' is a development build's command";
	if (name == "quit") {
		Log("control: quit requested");
		TerminateProcess(GetCurrentProcess(), 0);
	}
	CommandHandler handler;
	{
		std::lock_guard lock(s_mutex);
		auto it = s_commands.find(name);
		if (it != s_commands.end()) handler = it->second;
	}
	if (!handler) return "error unknown command '" + name + "'";
	return handler(args);
}

std::string JsonString(const std::string& s)
{
	std::string out = "\"";
	for (char c : s) {
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if ((unsigned char)c < 0x20) {
			char esc[8];
			snprintf(esc, sizeof(esc), "\\u%04x", c);
			out += esc;
		} else {
			out += c;
		}
	}
	return out + "\"";
}

void RegisterCommand(const std::string& name, CommandHandler handler)
{
	std::lock_guard lock(s_mutex);
	s_commands[name] = std::move(handler);
}

void RegisterStatus(const std::string& key, StatusField field)
{
	std::lock_guard lock(s_mutex);
	s_status.emplace_back(key, std::move(field));
}

void StartControlPipe(int instance)
{
	s_instance = instance;
	HANDLE thread = CreateThread(nullptr, 0, PipeThread, nullptr, 0, nullptr);
	if (thread) CloseHandle(thread);
	Log("control: listening on \\\\.\\pipe\\sfve-%d", instance);
}

} // namespace sfve::mod
