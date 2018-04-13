#include "OS.hpp"
#include "Log.hpp"

#include <cstdio>

namespace Xor
{
    void vprint(const char * fmt, va_list ap)
    {
        if (IsDebuggerPresent())
        {
            char msg[4096];
            vsprintf_s(msg, fmt, ap);
            OutputDebugStringA(msg);
        }
        else
        {
            vprintf_s(fmt, ap);
        }
    }

    void print(const char * fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        vprint(fmt, ap);
        va_end(ap);
    }

    void log(const char *tag, const char * fmt, ...)
    {
        if (tag)
            print("[%s]: ", tag);
        va_list ap;
        va_start(ap, fmt);
        vprint(fmt, ap);
        va_end(ap);
    }
}
