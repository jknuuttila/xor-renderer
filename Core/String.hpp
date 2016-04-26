#pragma once

#include "OS.hpp"

#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>

namespace xor
{
    // string.capitalize(word)
    // string.expandtabs(s[, tabsize])
	// string.find(s, sub[, start[, end]])
	// string.rfind(s, sub[, start[, end]])
	// string.count(s, sub[, start[, end]])
	// string.lower(s)
	// string.split(s[, sep[, maxsplit]])
	// string.join(words[, sep])
	// string.strip(s[, chars])
	// string.swapcase(s)
	// string.upper(s)
	// string.ljust(s, width[, fillchar])
	// string.rjust(s, width[, fillchar])
	// string.center(s, width[, fillchar])
	// string.replace(s, old, new[, maxreplace])

    static constexpr char Whitespace[] = " \t\r\n";

    class String;
    class StringView
    {
        const char *m_begin = nullptr;
        const char *m_end   = nullptr;
    public:
        StringView() = default;

        template <size_t N>
        StringView(const char (&stringLiteral)[N])
            : m_begin(stringLiteral)
            , m_end(m_begin + N - 1) // no null terminator
        {}

        StringView(const char *str)
            : m_begin(str)
            , m_end(str + std::strlen(str))
        {}

        StringView(const char *start, const char *end)
            : m_begin(start)
            , m_end(end)
        {}

        StringView(span<const char> chars)
            : m_begin(chars.data())
            , m_end(chars.data() + chars.size())
        {}

        StringView(const std::string &str)
            : m_begin(str.data())
            , m_end(m_begin + str.length())
        {}

        explicit operator bool() const { return m_begin != m_end; }
        bool empty() const { return m_begin == m_end; }

        size_t size() const { return m_end - m_begin; }
        int length() const { return static_cast<int>(size()); }

        const char *data()  const { return m_begin; }
        const char *begin() const { return m_begin; }
        const char *end()   const { return m_end; }

        const char &operator[](size_t i) const { return m_begin[i]; }

        bool operator==(StringView v) const
        {
            return size() == v.size() &&
                std::memcmp(data(), v.data(), size()) == 0;
        }

        bool operator!=(StringView v) const
        {
            return !operator==(v);
        }

        int compare(StringView v) const
        {
            size_t s1 = size();
            size_t s2 = v.size();
            size_t smaller = std::min(s1, s2);
            int result = std::memcmp(data(), v.data(), smaller);

            if (result != 0)
                return result;
            else if (s1 < s2)
                return -1;
            else 
                return static_cast<int>(s1 > s2);
        }

        bool operator< (StringView v) const { return compare(v) <  0; }
        bool operator<=(StringView v) const { return compare(v) <= 0; }
        bool operator> (StringView v) const { return compare(v) >  0; }
        bool operator>=(StringView v) const { return compare(v) >= 0; }

        StringView operator()(int start) const
        {
            int len = length();
            if (start < 0) start += len;
            XOR_ASSERT(start >= 0 && start <= len, "start out of range");

            return StringView(
                m_begin + start,
                m_end);
        }

        StringView operator()(int start, int end) const
        {
            int len = length();
            if (start < 0) start += len;
            if (end   < 0) end   += len;
            XOR_ASSERT(start >= 0 && start <= len, "start out of range");
            XOR_ASSERT(end   >= 0 && end   <= len, "end out of range");

            return StringView(
                m_begin + start,
                m_begin + end);
        }

        StringView first(int count) const
        {
            return StringView(
                m_begin,
                m_begin + std::min(count, length()));
        }

        int find(StringView sub, int start, int end) const;
        {
            auto len = length();
            if (start < 0) start += len;
            if (end   < 0) end   += len;

            XOR_ASSERT(start >= 0 && start <= len, "start out of range");
            XOR_ASSERT(end   >= 0 && end   <= len, "end out of range");

            auto subLen = sub.length();

            if (subLen > end)
                return -1;

            auto last = end - subLen;
            for (int i = 0; i <= last; ++i)
            {
                if (std::memcmp(data() + i, sub.data(), subLen) == 0)
                    return i;
            }

            return -1;
        }

        int find(StringView sub, int start) const { return find(sub, start, length()); }
        int find(StringView sub) const { return find(sub, 0); }

        String string() const;
        String capitalize() const;
        bool contains(StringView sub) const;
        int count(StringView sub) const;
        int count(StringView sub, int start) const;
        int count(StringView sub, int start, int end = -1) const;
        String lower() const;
        std::vector<String> split(StringView separators = Whitespace, int maxSplit = -1) const;
        String join(StringView separator = " ") const;
        String strip(StringView separators = Whitespace,
                     bool leftStrip = true,
                     bool rightStrip = true) const;
        String swapCase() const;
        String upper() const;
        String leftJustify(int width, char filler = ' ') const;
        String rightJustify(int width, char filler = ' ') const;
        String center(int width, char filler = ' ') const;
        String replace(StringView old, StringView replacement, int maxReplace = -1) const;
    };

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
