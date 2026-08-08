#pragma once

// Diagnostic logging. Writes to Logs\Interact.log next to Wow.exe.
//
// Set INTERACT_LOGGING to 0 to compile it out entirely once we're done
// debugging -- the calls disappear, no runtime cost.

// 0 = off (shipping). Every LOG() call compiles away to nothing, arguments
// included, so there is no runtime cost and no log file is created.
// Set to 1 if something needs diagnosing again.
#define INTERACT_LOGGING 0

#if INTERACT_LOGGING

#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace Log
{
    inline void Write(const char* fmt, ...)
    {
        FILE* f = nullptr;
        if (fopen_s(&f, "Logs\\Interact.log", "a") != 0 || !f) return;

        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);

        fputc('\n', f);
        fclose(f);
    }
}

#define LOG(...) Log::Write(__VA_ARGS__)

#else
#define LOG(...) ((void)0)
#endif
