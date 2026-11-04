#pragma once

// The core's one logging hook. The mod points it at its log file; tests leave
// it on stderr.

#include <stdarg.h>
#include <stdio.h>

namespace sfve {

using LogSink = void (*)(const char* line);

inline LogSink& CoreLogSink()
{
	static LogSink sink = nullptr;
	return sink;
}

inline void CoreLog(const char* fmt, ...)
{
	char line[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	if (CoreLogSink()) {
		CoreLogSink()(line);
	} else {
		fprintf(stderr, "%s\n", line);
	}
}

} // namespace sfve
