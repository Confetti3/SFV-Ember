// GGPO's assertions in the test programs that run sessions. In the game the
// crash record handles them (mod/CrashRecord.hxx); the session itself sets no
// handler. Without one, GGPO's Windows build shows a message box, which would
// hang an unattended run: here the assertion is printed and GGPO's exit(1)
// then ends the run as failed.

#include <windows.h>

#include <ggponet.h>

#include <stdio.h>

namespace {

void __cdecl OnGgpoAssertion(const char* message)
{
	fprintf(stderr, "GGPO assertion failed: %s\n", message ? message : "?");
	fflush(stderr);
}

const bool s_installed = [] {
	ggpo_set_assert_handler(&OnGgpoAssertion);
	return true;
}();

} // namespace
