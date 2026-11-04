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

std::string Handle(const std::string& line)
{
	const size_t space = line.find(' ');
	const std::string name = line.substr(0, space);
	const std::string args = space == std::string::npos ? "" : line.substr(space + 1);
	if (name == "ping") return "ok pong";
	if (name == "status") return "ok " + Status();
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

DWORD WINAPI PipeThread(void*)
{
	const std::wstring path = L"\\\\.\\pipe\\sfve-" + std::to_wstring(s_instance);
	for (;;) {
		HANDLE pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
			1, 64 * 1024, 64 * 1024, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE) {
			Log("control: cannot create %ls (%lu)", path.c_str(), GetLastError());
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
					std::string reply = Handle(line) + "\n";
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
