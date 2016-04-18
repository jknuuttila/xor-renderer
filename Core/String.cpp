#include "String.hpp"

#include <vector>
#include <cstdlib>
#include <locale>
#include <codecvt>

namespace xor
{
    static std::wstring_convert<std::codecvt_utf8<wchar_t>> wConverter;

    String::String(const std::wstring &wstr)
        : std::string(wConverter.to_bytes(wstr))
    {}

    String::String(const wchar_t *wstr)
        : std::string(wConverter.to_bytes(wstr))
    {}

    std::wstring String::wideStr() const
    {
        return wConverter.from_bytes(*this);
    }

    std::string replaceAll(std::string s, const std::string &replacedString, const std::string &replaceWith)
    {
        auto oldLen = replacedString.length();
        auto newLen = replaceWith.length();

        auto pos = s.find(replacedString, 0);

        while (pos != std::string::npos)
        {
            s.replace(pos, oldLen, replaceWith);
            pos += newLen;
            pos = s.find(replacedString, pos);
        }

        return s;
    }

    std::vector<std::string> tokenize(const std::string &s, const std::string &delimiters)
    {
        std::vector<std::string> tokens;

        std::string::size_type pos = 0;
        bool delim = false;

        while (pos != std::string::npos)
        {
            if (delim)
            {
                pos = s.find_first_not_of(delimiters, pos);
            }
            else
            {
                auto end = s.find_first_of(delimiters, pos);
                if (end == std::string::npos) end = s.length();
                tokens.emplace_back(s.substr(pos, end - pos));
                if (tokens.back().empty())
                    tokens.pop_back();
                pos = end;
            }

            delim = !delim;
        }

        return tokens;
    }

}
