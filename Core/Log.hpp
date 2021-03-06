#pragma once

namespace Xor
{
    void vprint(const char *fmt, va_list ap);
    void print(const char *fmt, ...);
    void log(const char *tag, const char *fmt, ...);
}

