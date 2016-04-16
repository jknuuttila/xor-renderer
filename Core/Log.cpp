#include "OS.hpp"
#include "Log.hpp"

#include <cstdio>

namespace xor
{
    void vlog(const char * fmt, va_list ap)
    {
        if (IsDebuggerPresent())
        {
            char msg[1024];
            vsprintf_s(msg, fmt, ap);
            OutputDebugStringA(msg);
        }
        else
        {
            vprintf_s(fmt, ap);
        }
    }

    void log(const char * fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        vlog(fmt, ap);
        va_end(ap);
    }
}
