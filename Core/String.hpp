#pragma once

#include "OS.hpp"

#include <string>
#include <vector>

namespace xor
{
    class String : public std::string
    {
    public:
        using std::string::string;

        String() {}
        String(const std::string &str) : std::string(str) {}
        explicit String(const std::wstring &wstr);
        explicit String(const wchar_t *wstr);

        static String format(const char *fmt, ...);

        template <typename... Ts>
        static String format(const String &fmt, Ts... ts)
        {
            return format(fmt.c_str(), ts...);
        }

        String operator+(const String &s)
        {
            return static_cast<const std::string &>(*this) +
                static_cast<const std::string &>(s);
        }

        explicit operator bool() const { return !empty(); }
        std::wstring wideStr() const;
    };

    // TODO: Make a StringView class and move these there.
    String replaceAll(String s,
                      const String &replacedString,
                      const String &replaceWith);
    std::vector<String> tokenize(const String &s, const String &delimiters);

    template <typename Iter>
    String join(Iter begin, Iter end, String separator)
    {
        String s;

        if (begin == end)
            return s;

        auto it = begin;
        s = *it;
        ++it;

        while (it != end)
        {
            s += separator;
            s += *it;
            ++it;
        }

        return s;
    }
}
