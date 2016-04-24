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
        explicit String(const std::wstring &wstr);
        explicit String(const wchar_t *wstr);

        static String format(const char *fmt, ...);

        template <typename... Ts>
        static String format(const String &fmt, Ts... ts)
        {
            return format(fmt.c_str(), ts...);
        }

        std::wstring wideStr() const;
    };

    // TODO: Make a StringView class and move these there.
    std::string replaceAll(std::string s, const std::string &replacedString, const std::string &replaceWith);
    std::vector<std::string> tokenize(const std::string &s, const std::string &delimiters);

    template <typename Iter>
    std::string join(Iter begin, Iter end, std::string separator)
    {
        std::string s;

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
