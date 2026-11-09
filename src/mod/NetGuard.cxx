#include "mod/NetGuard.hxx"

#include "mod/GameHooks.hxx"
#include "mod/LagSim.hxx"
#include "mod/Log.hxx"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windns.h>

#include <MinHook.h>

#include <atomic>
#include <mutex>
#include <string>

namespace sfve::mod {

namespace {

net::NetPolicy s_policy;
std::atomic<uint64_t> s_refusals{0};

// --- refusal log ------------------------------------------------------------

void Refused(const char* api, const std::string& what)
{
	const uint64_t n = ++s_refusals;
	if (n <= 50 || n % 100 == 0) Log("netguard: refused %s %s (refusal %llu)", api, what.c_str(), n);
}

std::string AddressText(const sockaddr* addr, int len)
{
	char text[INET6_ADDRSTRLEN + 16] = "?";
	if (addr && addr->sa_family == AF_INET && len >= (int)sizeof(sockaddr_in)) {
		const auto* in = reinterpret_cast<const sockaddr_in*>(addr);
		inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text));
		return std::string(text) + ":" + std::to_string(ntohs(in->sin_port));
	}
	if (addr && addr->sa_family == AF_INET6 && len >= (int)sizeof(sockaddr_in6)) {
		const auto* in6 = reinterpret_cast<const sockaddr_in6*>(addr);
		inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof(text));
		return "[" + std::string(text) + "]:" + std::to_string(ntohs(in6->sin6_port));
	}
	return addr ? "family " + std::to_string(addr->sa_family) : "(null)";
}

std::string Narrow(const wchar_t* w)
{
	std::string s;
	for (; w && *w; w++) s += *w < 0x80 ? (char)*w : '?';
	return s;
}

// Host part of an http(s) URL, for WinINet's URL entry points.
std::wstring UrlHost(const wchar_t* url)
{
	std::wstring u = url ? url : L"";
	size_t start = u.find(L"://");
	start = start == std::wstring::npos ? 0 : start + 3;
	size_t end = u.find_first_of(L":/?#", start);
	if (end == std::wstring::npos) end = u.size();
	if (start < u.size() && u[start] == L'[') {
		const size_t close = u.find(L']', start);
		if (close != std::wstring::npos) return u.substr(start + 1, close - start - 1);
	}
	return u.substr(start, end - start);
}

std::wstring Widen(const char* s)
{
	std::wstring w;
	for (; s && *s; s++) w += (wchar_t)(unsigned char)*s;
	return w;
}

// --- Winsock ----------------------------------------------------------------

using connect_t = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using WSAConnect_t = int(WSAAPI*)(SOCKET, const sockaddr*, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS);
using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using WSASendTo_t = int(WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using getaddrinfo_t = INT(WSAAPI*)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
using GetAddrInfoW_t = INT(WSAAPI*)(PCWSTR, PCWSTR, const ADDRINFOW*, PADDRINFOW*);
using GetAddrInfoExW_t = INT(WSAAPI*)(PCWSTR, PCWSTR, DWORD, LPGUID, const ADDRINFOEXW*, PADDRINFOEXW*, timeval*,
	LPOVERLAPPED, LPLOOKUPSERVICE_COMPLETION_ROUTINE, LPHANDLE);
using gethostbyname_t = hostent*(WSAAPI*)(const char*);
using WSAIoctl_t = int(WSAAPI*)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPWSAOVERLAPPED,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE);

connect_t o_connect;
WSAConnect_t o_WSAConnect;
sendto_t o_sendto;
WSASendTo_t o_WSASendTo;
getaddrinfo_t o_getaddrinfo;
GetAddrInfoW_t o_GetAddrInfoW;
GetAddrInfoExW_t o_GetAddrInfoExW;
gethostbyname_t o_gethostbyname;
WSAIoctl_t o_WSAIoctl;

int WSAAPI h_connect(SOCKET s, const sockaddr* name, int len)
{
	if (!s_policy.AllowsAddress(name, len)) {
		Refused("connect", AddressText(name, len));
		WSASetLastError(WSAENETUNREACH);
		return SOCKET_ERROR;
	}
	return o_connect(s, name, len);
}

int WSAAPI h_WSAConnect(SOCKET s, const sockaddr* name, int len, LPWSABUF a, LPWSABUF b, LPQOS c, LPQOS d)
{
	if (!s_policy.AllowsAddress(name, len)) {
		Refused("WSAConnect", AddressText(name, len));
		WSASetLastError(WSAENETUNREACH);
		return SOCKET_ERROR;
	}
	return o_WSAConnect(s, name, len, a, b, c, d);
}

int WSAAPI h_sendto(SOCKET s, const char* buf, int n, int flags, const sockaddr* to, int tolen)
{
	// A null destination sends on a connected socket, whose connect was checked.
	if (to && !s_policy.AllowsAddress(to, tolen)) {
		Refused("sendto", AddressText(to, tolen));
		WSASetLastError(WSAENETUNREACH);
		return SOCKET_ERROR;
	}
	if (LagSimQueue(s, buf, n, flags, to, tolen)) return n;
	return o_sendto(s, buf, n, flags, to, tolen);
}

int WSAAPI h_WSASendTo(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD flags, const sockaddr* to,
	int tolen, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE done)
{
	if (to && !s_policy.AllowsAddress(to, tolen)) {
		Refused("WSASendTo", AddressText(to, tolen));
		WSASetLastError(WSAENETUNREACH);
		return SOCKET_ERROR;
	}
	return o_WSASendTo(s, bufs, count, sent, flags, to, tolen, ov, done);
}

INT WSAAPI h_getaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA* hints, PADDRINFOA* result)
{
	if (!s_policy.AllowsHostName(node)) {
		Refused("getaddrinfo", node);
		return WSAHOST_NOT_FOUND;
	}
	return o_getaddrinfo(node, service, hints, result);
}

INT WSAAPI h_GetAddrInfoW(PCWSTR node, PCWSTR service, const ADDRINFOW* hints, PADDRINFOW* result)
{
	if (!s_policy.AllowsHostNameW(node)) {
		Refused("GetAddrInfoW", Narrow(node));
		return WSAHOST_NOT_FOUND;
	}
	return o_GetAddrInfoW(node, service, hints, result);
}

INT WSAAPI h_GetAddrInfoExW(PCWSTR name, PCWSTR service, DWORD ns, LPGUID id, const ADDRINFOEXW* hints,
	PADDRINFOEXW* result, timeval* timeout, LPOVERLAPPED ov, LPLOOKUPSERVICE_COMPLETION_ROUTINE done, LPHANDLE handle)
{
	if (!s_policy.AllowsHostNameW(name)) {
		Refused("GetAddrInfoExW", Narrow(name));
		return WSAHOST_NOT_FOUND;
	}
	return o_GetAddrInfoExW(name, service, ns, id, hints, result, timeout, ov, done, handle);
}

using WSAConnectByNameW_t = BOOL(WSAAPI*)(SOCKET, LPWSTR, LPWSTR, LPDWORD, LPSOCKADDR, LPDWORD, LPSOCKADDR,
	const timeval*, LPWSAOVERLAPPED);
using WSAConnectByNameA_t = BOOL(WSAAPI*)(SOCKET, LPCSTR, LPCSTR, LPDWORD, LPSOCKADDR, LPDWORD, LPSOCKADDR,
	const timeval*, LPWSAOVERLAPPED);
WSAConnectByNameW_t o_WSAConnectByNameW;
WSAConnectByNameA_t o_WSAConnectByNameA;

BOOL WSAAPI h_WSAConnectByNameW(SOCKET s, LPWSTR node, LPWSTR service, LPDWORD ll, LPSOCKADDR la, LPDWORD rl,
	LPSOCKADDR ra, const timeval* timeout, LPWSAOVERLAPPED reserved)
{
	if (!node || !s_policy.AllowsHostNameW(node)) {
		Refused("WSAConnectByNameW", Narrow(node));
		WSASetLastError(WSAHOST_NOT_FOUND);
		return FALSE;
	}
	return o_WSAConnectByNameW(s, node, service, ll, la, rl, ra, timeout, reserved);
}

BOOL WSAAPI h_WSAConnectByNameA(SOCKET s, LPCSTR node, LPCSTR service, LPDWORD ll, LPSOCKADDR la, LPDWORD rl,
	LPSOCKADDR ra, const timeval* timeout, LPWSAOVERLAPPED reserved)
{
	if (!node || !s_policy.AllowsHostName(node)) {
		Refused("WSAConnectByNameA", node ? node : "(null)");
		WSASetLastError(WSAHOST_NOT_FOUND);
		return FALSE;
	}
	return o_WSAConnectByNameA(s, node, service, ll, la, rl, ra, timeout, reserved);
}

hostent* WSAAPI h_gethostbyname(const char* name)
{
	if (!s_policy.AllowsHostName(name)) {
		Refused("gethostbyname", name ? name : "(null)");
		WSASetLastError(WSAHOST_NOT_FOUND);
		return nullptr;
	}
	return o_gethostbyname(name);
}

// ConnectEx is not exported: programs fetch it per provider through WSAIoctl.
// Each distinct pointer handed out gets its own guarded wrapper.
using ConnectEx_t = BOOL(PASCAL*)(SOCKET, const sockaddr*, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);
constexpr int kConnectExSlots = 4;
ConnectEx_t o_connectEx[kConnectExSlots];
std::mutex s_connectExMutex;

template <int N>
BOOL PASCAL h_ConnectEx(SOCKET s, const sockaddr* name, int len, PVOID buf, DWORD n, LPDWORD sent, LPOVERLAPPED ov)
{
	if (!s_policy.AllowsAddress(name, len)) {
		Refused("ConnectEx", AddressText(name, len));
		WSASetLastError(WSAENETUNREACH);
		return FALSE;
	}
	return o_connectEx[N](s, name, len, buf, n, sent, ov);
}

constexpr ConnectEx_t kConnectExWrappers[kConnectExSlots] = {h_ConnectEx<0>, h_ConnectEx<1>, h_ConnectEx<2>,
	h_ConnectEx<3>};

int WSAAPI h_WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD inLen, LPVOID out, DWORD outLen, LPDWORD returned,
	LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE done)
{
	const int result = o_WSAIoctl(s, code, in, inLen, out, outLen, returned, ov, done);
	static const GUID connectEx = WSAID_CONNECTEX;
	if (result != 0 || code != SIO_GET_EXTENSION_FUNCTION_POINTER || !in || inLen < sizeof(GUID) || !out ||
		outLen < sizeof(void*) || memcmp(in, &connectEx, sizeof(GUID)) != 0) {
		return result;
	}
	ConnectEx_t real = *static_cast<ConnectEx_t*>(out);
	std::lock_guard lock(s_connectExMutex);
	for (int i = 0; i < kConnectExSlots; i++) {
		if (o_connectEx[i] == real || !o_connectEx[i]) {
			o_connectEx[i] = real;
			*static_cast<ConnectEx_t*>(out) = kConnectExWrappers[i];
			return result;
		}
	}
	// More providers than slots: hand out nothing, so the caller uses connect.
	Log("netguard: refused a fifth ConnectEx provider");
	*static_cast<ConnectEx_t*>(out) = nullptr;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

// --- DNS client API ---------------------------------------------------------

using DnsQuery_W_t = DNS_STATUS(WINAPI*)(PCWSTR, WORD, DWORD, PVOID, PDNS_RECORD*, PVOID*);
using DnsQuery_A_t = DNS_STATUS(WINAPI*)(PCSTR, WORD, DWORD, PVOID, PDNS_RECORD*, PVOID*);
using DnsQueryEx_t = DNS_STATUS(WINAPI*)(PDNS_QUERY_REQUEST, PDNS_QUERY_RESULT, PDNS_QUERY_CANCEL);

DnsQuery_W_t o_DnsQuery_W;
DnsQuery_A_t o_DnsQuery_A;
DnsQuery_A_t o_DnsQuery_UTF8;
DnsQueryEx_t o_DnsQueryEx;

DNS_STATUS WINAPI h_DnsQuery_W(PCWSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD* out, PVOID* r)
{
	if (!s_policy.AllowsHostNameW(name)) {
		Refused("DnsQuery_W", Narrow(name));
		return DNS_ERROR_RCODE_NAME_ERROR;
	}
	return o_DnsQuery_W(name, type, options, extra, out, r);
}

DNS_STATUS WINAPI h_DnsQuery_A(PCSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD* out, PVOID* r)
{
	if (!s_policy.AllowsHostName(name)) {
		Refused("DnsQuery_A", name ? name : "(null)");
		return DNS_ERROR_RCODE_NAME_ERROR;
	}
	return o_DnsQuery_A(name, type, options, extra, out, r);
}

DNS_STATUS WINAPI h_DnsQuery_UTF8(PCSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORD* out, PVOID* r)
{
	if (!s_policy.AllowsHostName(name)) {
		Refused("DnsQuery_UTF8", name ? name : "(null)");
		return DNS_ERROR_RCODE_NAME_ERROR;
	}
	return o_DnsQuery_UTF8(name, type, options, extra, out, r);
}

DNS_STATUS WINAPI h_DnsQueryEx(PDNS_QUERY_REQUEST request, PDNS_QUERY_RESULT result, PDNS_QUERY_CANCEL cancel)
{
	const wchar_t* name = request ? request->QueryName : nullptr;
	if (!name || !s_policy.AllowsHostNameW(name)) {
		Refused("DnsQueryEx", Narrow(name));
		return DNS_ERROR_RCODE_NAME_ERROR;
	}
	return o_DnsQueryEx(request, result, cancel);
}

// --- WinHTTP and WinINet ----------------------------------------------------
// Declared by hand: winhttp.h and wininet.h cannot be included together.

using HINET = void*;
constexpr DWORD kNameNotResolved = 12007; // ERROR_WINHTTP_/ERROR_INTERNET_NAME_NOT_RESOLVED

using WinHttpConnect_t = HINET(WINAPI*)(HINET, LPCWSTR, WORD, DWORD);
using InternetConnectW_t = HINET(WINAPI*)(HINET, LPCWSTR, WORD, LPCWSTR, LPCWSTR, DWORD, DWORD, DWORD_PTR);
using InternetConnectA_t = HINET(WINAPI*)(HINET, LPCSTR, WORD, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
using InternetOpenUrlW_t = HINET(WINAPI*)(HINET, LPCWSTR, LPCWSTR, DWORD, DWORD, DWORD_PTR);
using InternetOpenUrlA_t = HINET(WINAPI*)(HINET, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);

WinHttpConnect_t o_WinHttpConnect;
InternetConnectW_t o_InternetConnectW;
InternetConnectA_t o_InternetConnectA;
InternetOpenUrlW_t o_InternetOpenUrlW;
InternetOpenUrlA_t o_InternetOpenUrlA;

HINET WINAPI h_WinHttpConnect(HINET session, LPCWSTR server, WORD port, DWORD reserved)
{
	if (!s_policy.AllowsHostNameW(server)) {
		Refused("WinHttpConnect", Narrow(server));
		SetLastError(kNameNotResolved);
		return nullptr;
	}
	return o_WinHttpConnect(session, server, port, reserved);
}

HINET WINAPI h_InternetConnectW(HINET h, LPCWSTR server, WORD port, LPCWSTR user, LPCWSTR pass, DWORD service,
	DWORD flags, DWORD_PTR ctx)
{
	if (!s_policy.AllowsHostNameW(server)) {
		Refused("InternetConnectW", Narrow(server));
		SetLastError(kNameNotResolved);
		return nullptr;
	}
	return o_InternetConnectW(h, server, port, user, pass, service, flags, ctx);
}

HINET WINAPI h_InternetConnectA(HINET h, LPCSTR server, WORD port, LPCSTR user, LPCSTR pass, DWORD service,
	DWORD flags, DWORD_PTR ctx)
{
	if (!s_policy.AllowsHostName(server)) {
		Refused("InternetConnectA", server ? server : "(null)");
		SetLastError(kNameNotResolved);
		return nullptr;
	}
	return o_InternetConnectA(h, server, port, user, pass, service, flags, ctx);
}

HINET WINAPI h_InternetOpenUrlW(HINET h, LPCWSTR url, LPCWSTR headers, DWORD len, DWORD flags, DWORD_PTR ctx)
{
	const std::wstring host = UrlHost(url);
	if (!s_policy.AllowsHostNameW(host.c_str())) {
		Refused("InternetOpenUrlW", Narrow(host.c_str()));
		SetLastError(kNameNotResolved);
		return nullptr;
	}
	return o_InternetOpenUrlW(h, url, headers, len, flags, ctx);
}

HINET WINAPI h_InternetOpenUrlA(HINET h, LPCSTR url, LPCSTR headers, DWORD len, DWORD flags, DWORD_PTR ctx)
{
	const std::wstring host = UrlHost(Widen(url).c_str());
	if (!s_policy.AllowsHostNameW(host.c_str())) {
		Refused("InternetOpenUrlA", Narrow(host.c_str()));
		SetLastError(kNameNotResolved);
		return nullptr;
	}
	return o_InternetOpenUrlA(h, url, headers, len, flags, ctx);
}

// --- installation -----------------------------------------------------------

struct HookSpec {
	const wchar_t* module;
	const char* name;
	void* detour;
	void** original;
};

template <typename T>
void** Slot(T* p)
{
	return reinterpret_cast<void**>(p);
}

} // namespace

void StartGuardedLagSim(int delayMs)
{
	StartLagSim(delayMs, o_sendto);
}

net::NetPolicy& GuardPolicy()
{
	return s_policy;
}

uint64_t NetGuardRefusals()
{
	return s_refusals.load();
}

bool InstallNetGuard()
{
	const HookSpec hooks[] = {
		{L"ws2_32.dll", "connect", (void*)h_connect, Slot(&o_connect)},
		{L"ws2_32.dll", "WSAConnect", (void*)h_WSAConnect, Slot(&o_WSAConnect)},
		{L"ws2_32.dll", "sendto", (void*)h_sendto, Slot(&o_sendto)},
		{L"ws2_32.dll", "WSASendTo", (void*)h_WSASendTo, Slot(&o_WSASendTo)},
		{L"ws2_32.dll", "getaddrinfo", (void*)h_getaddrinfo, Slot(&o_getaddrinfo)},
		{L"ws2_32.dll", "GetAddrInfoW", (void*)h_GetAddrInfoW, Slot(&o_GetAddrInfoW)},
		{L"ws2_32.dll", "GetAddrInfoExW", (void*)h_GetAddrInfoExW, Slot(&o_GetAddrInfoExW)},
		{L"ws2_32.dll", "gethostbyname", (void*)h_gethostbyname, Slot(&o_gethostbyname)},
		{L"ws2_32.dll", "WSAIoctl", (void*)h_WSAIoctl, Slot(&o_WSAIoctl)},
		{L"ws2_32.dll", "WSAConnectByNameW", (void*)h_WSAConnectByNameW, Slot(&o_WSAConnectByNameW)},
		{L"ws2_32.dll", "WSAConnectByNameA", (void*)h_WSAConnectByNameA, Slot(&o_WSAConnectByNameA)},
		{L"dnsapi.dll", "DnsQuery_W", (void*)h_DnsQuery_W, Slot(&o_DnsQuery_W)},
		{L"dnsapi.dll", "DnsQuery_A", (void*)h_DnsQuery_A, Slot(&o_DnsQuery_A)},
		{L"dnsapi.dll", "DnsQuery_UTF8", (void*)h_DnsQuery_UTF8, Slot(&o_DnsQuery_UTF8)},
		{L"dnsapi.dll", "DnsQueryEx", (void*)h_DnsQueryEx, Slot(&o_DnsQueryEx)},
		{L"winhttp.dll", "WinHttpConnect", (void*)h_WinHttpConnect, Slot(&o_WinHttpConnect)},
		{L"wininet.dll", "InternetConnectW", (void*)h_InternetConnectW, Slot(&o_InternetConnectW)},
		{L"wininet.dll", "InternetConnectA", (void*)h_InternetConnectA, Slot(&o_InternetConnectA)},
		{L"wininet.dll", "InternetOpenUrlW", (void*)h_InternetOpenUrlW, Slot(&o_InternetOpenUrlW)},
		{L"wininet.dll", "InternetOpenUrlA", (void*)h_InternetOpenUrlA, Slot(&o_InternetOpenUrlA)},
	};

	char computer[256];
	DWORD computerLen = sizeof(computer);
	if (GetComputerNameExA(ComputerNameDnsHostname, computer, &computerLen)) s_policy.AllowLocalName(computer);

	const MH_STATUS init = MH_Initialize();
	if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
		Log("netguard: MinHook did not initialize (%d)", (int)init);
		return false;
	}

	// One set: every route on a module that loads, or the guard is not in and
	// the caller ends the process. An export missing from a loaded module
	// fails the set too (HookTransaction::Add), so no route is left open.
	HookTransaction guard;
	for (const HookSpec& h : hooks) {
		// Loading the module now means a later load by the game finds it
		// already hooked. A module Windows cannot load, the game cannot use.
		const HMODULE module = LoadLibraryW(h.module);
		if (!module) {
			Log("netguard: %ls not available; nothing to guard there", h.module);
			continue;
		}
		guard.Add(h.name, (void*)GetProcAddress(module, h.name), h.detour, h.original);
	}
	const bool ok = !CommitHookSet(guard, "network guard");
	Log("netguard: %s", ok ? "installed; only loopback and allowed peers are reachable" : "NOT installed");
	return ok;
}

} // namespace sfve::mod
