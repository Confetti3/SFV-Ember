#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <winsock2.h>

int main(int argc, char** argv)
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	doctest::Context context(argc, argv);
	const int result = context.run();
	WSACleanup();
	return result;
}
