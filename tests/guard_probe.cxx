// Stand-in for StreetFighterV.exe: imports xinput1_3 statically, so Windows
// loads the proxy before this exe's entry point, exactly as for the game. By
// the time main() runs the network guard must be in place.
//
// Exit code 0 when: an internet connect is refused, internet name lookups
// fail, and loopback still works. Any other result fails the CTest.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // gethostbyname is exactly what is tested
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#include <stdio.h>

extern "C" DWORD WINAPI XInputGetState(DWORD user, void* state);

namespace {

int failures = 0;

void Expect(bool ok, const char* what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) failures++;
}

} // namespace

int main()
{
	unsigned char state[16] = {};
	XInputGetState(0, state); // keeps the import

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	// Internet TCP connect.
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in out = {};
	out.sin_family = AF_INET;
	out.sin_port = htons(443);
	inet_pton(AF_INET, "1.1.1.1", &out.sin_addr);
	const int rc = connect(s, (sockaddr*)&out, sizeof(out));
	Expect(rc == SOCKET_ERROR && WSAGetLastError() == WSAENETUNREACH, "connect to 1.1.1.1:443 refused by the guard");
	closesocket(s);

	// Internet UDP send.
	SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	const int sent = sendto(u, "x", 1, 0, (sockaddr*)&out, sizeof(out));
	Expect(sent == SOCKET_ERROR && WSAGetLastError() == WSAENETUNREACH, "UDP send to 1.1.1.1 refused");

	// Name lookups.
	addrinfo* result = nullptr;
	Expect(getaddrinfo("www.capcom.com", "443", nullptr, &result) != 0, "getaddrinfo(www.capcom.com) fails");
	Expect(gethostbyname("www.capcom.com") == nullptr, "gethostbyname(www.capcom.com) fails");
	ADDRINFOW* wresult = nullptr;
	Expect(GetAddrInfoW(L"example.com", L"80", nullptr, &wresult) != 0, "GetAddrInfoW(example.com) fails");

	// Loopback still works: UDP to ourselves.
	sockaddr_in local = {};
	local.sin_family = AF_INET;
	inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
	bind(u, (sockaddr*)&local, sizeof(local));
	int len = sizeof(local);
	getsockname(u, (sockaddr*)&local, &len);
	Expect(sendto(u, "y", 1, 0, (sockaddr*)&local, sizeof(local)) == 1, "UDP to loopback allowed");
	char buf[4];
	Expect(recv(u, buf, sizeof(buf), 0) == 1 && buf[0] == 'y', "loopback datagram received");
	Expect(getaddrinfo("localhost", "80", nullptr, &result) == 0, "getaddrinfo(localhost) works");

	// WSASendMsg, the exported one and the provider's (fetched through
	// WSAIoctl, as ConnectEx is): the same policy as sendto.
	char payload[] = "z";
	WSABUF buffer = {1, payload};
	WSAMSG toInternet = {};
	toInternet.name = (LPSOCKADDR)&out;
	toInternet.namelen = sizeof(out);
	toInternet.lpBuffers = &buffer;
	toInternet.dwBufferCount = 1;
	WSAMSG toLoopback = toInternet;
	toLoopback.name = (LPSOCKADDR)&local;
	toLoopback.namelen = sizeof(local);
	DWORD sentBytes = 0;
	Expect(WSASendMsg(u, &toInternet, 0, &sentBytes, nullptr, nullptr) == SOCKET_ERROR && WSAGetLastError() == WSAENETUNREACH,
		"WSASendMsg to 1.1.1.1 refused");
	Expect(WSASendMsg(u, &toLoopback, 0, &sentBytes, nullptr, nullptr) == 0 && sentBytes == 1, "WSASendMsg to loopback allowed");
	Expect(recv(u, buf, sizeof(buf), 0) == 1 && buf[0] == 'z', "loopback WSASendMsg datagram received");
	GUID sendMsgId = WSAID_WSASENDMSG;
	LPFN_WSASENDMSG provider = nullptr;
	DWORD returned = 0;
	Expect(WSAIoctl(u, SIO_GET_EXTENSION_FUNCTION_POINTER, &sendMsgId, sizeof(sendMsgId), &provider, sizeof(provider), &returned,
			   nullptr, nullptr) == 0 && provider,
		"the provider's WSASendMsg is handed out");
	Expect(provider && provider(u, &toInternet, 0, &sentBytes, nullptr, nullptr) == SOCKET_ERROR && WSAGetLastError() == WSAENETUNREACH,
		"the provider's WSASendMsg to 1.1.1.1 refused");
	sentBytes = 0;
	Expect(provider && provider(u, &toLoopback, 0, &sentBytes, nullptr, nullptr) == 0 && sentBytes == 1,
		"the provider's WSASendMsg to loopback allowed");
	Expect(recv(u, buf, sizeof(buf), 0) == 1 && buf[0] == 'z', "loopback datagram of the provider's WSASendMsg received");
	GUID rioId = WSAID_MULTIPLE_RIO;
	RIO_EXTENSION_FUNCTION_TABLE rio = {};
	Expect(WSAIoctl(u, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &rioId, sizeof(rioId), &rio, sizeof(rio), &returned, nullptr,
			   nullptr) == SOCKET_ERROR && WSAGetLastError() == WSAEOPNOTSUPP,
		"the registered I/O function table is not handed out");
	closesocket(u);

	WSACleanup();
	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
