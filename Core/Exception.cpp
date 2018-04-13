#include "Core/Exception.hpp"
#include "Core/Log.hpp"

#include <cstdarg>

namespace Xor
{
    void Exception::printErrorMessage(const char *file, int line)
    {
        if (file && line > 0)
            print("%s(%d): ERROR: %s\n", file, line, m_error.cStr());
        else if (m_error)
            print("ERROR: %s\n", m_error.cStr());
    }

    Exception::Exception(String error)
        : m_error(std::move(error))
    {
        printErrorMessage();
    }

    Exception::Exception(const char * fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        m_error = String::vformat(fmt, ap);
        printErrorMessage();
        va_end(ap);
    }

    Exception::Exception(const char * file, int line, String error)
        : m_error(std::move(error))
    {
        printErrorMessage(file, line);
    }

    Exception::Exception(const char * file, int line, const char * fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        m_error = String::vformat(fmt, ap);
        printErrorMessage(file, line);
        va_end(ap);
    }

    const char * Exception::what() const
    {
        return m_error.cStr();
    }
}
