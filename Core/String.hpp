#pragma once

#include "OS.hpp"
#include "Utils.hpp"
#include "Error.hpp"

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

        int idx(int i) const
        {
            int len = length();
            if (i < 0) i += len;
            XOR_ASSERT(i >= 0 && i <= len, "index out of range");
            return i;
        }

        int findLoop(StringView sub, int start, int end, int inc) const
        {
            int L = sub.length();

            for (int i = start; i != end; i += inc)
            {
                if (std::memcmp(data() + i, sub.data(), L) == 0)
                    return i;
            }

            return -1;
        }
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

        String str() const;
        std::wstring wideStr() const;

        bool operator==(StringView v) const
        {
            return size() == v.size() &&
                std::memcmp(data(), v.data(), size()) == 0;
        }

        bool operator!=(StringView v) const
        {
            return !operator==(v);
        }

        friend int compare(StringView a, StringView b) const
        {
            size_t sa = a.size();
            size_t sb = b.size();
            size_t smaller = std::min(sa, sb);
            int result = std::memcmp(a.data(), b.data(), smaller);

            if (result != 0)
                return result;
            else if (sa < sb)
                return -1;
            else 
                return static_cast<int>(sa > sb);
        }

        bool operator< (StringView v) const { return compare(*this, v) <  0; }
        bool operator<=(StringView v) const { return compare(*this, v) <= 0; }
        bool operator> (StringView v) const { return compare(*this, v) >  0; }
        bool operator>=(StringView v) const { return compare(*this, v) >= 0; }

        StringView operator()(int start, int end) const
        {
            return StringView(
                m_begin + idx(start),
                m_begin + idx(end));
        }

        StringView from(int start) const
        {
            return StringView(
                m_begin + idx(start),
                m_end);
        }
        StringView until(int end) const
        {
            return StringView(
                m_begin,
                m_begin + idx(end));
        }

        int find(StringView sub, int start, int end) const
        {
            start = idx(start);
            end   = idx(end);

            if (end - start < sub.length())
                return -1;
            else
                end -= sub.length();

            return findLoop(sub, start, end, 1);
        }
        int find(StringView sub, int start) const { return find(sub, start, length()); }
        int find(StringView sub) const { return find(sub, 0); }

        bool contains(StringView sub) const
        {
            return find(sub) >= 0;
        }

        int count(StringView sub, int start, int end) const
        {
            start = idx(start);
            end   = idx(end);

            if (end - start < sub.length())
                return 0;
            else
                end -= sub.length();

            int amount = 0;
            for (;;)
            {
                start = findLoop(sub, start, end, 1);
                if (start < 0)
                    return amount;
                else
                    ++amount;
            }
        }
        int count(StringView sub, int start) const { return count(sub, start, length()); }
        int count(StringView sub) const { return count(sub, 0); }

        int rfind(StringView sub, int start, int end) const
        {
            start = idx(start);
            end   = idx(end);

            if (end - start < sub.length())
                return -1;
            else
                end -= sub.length();

            return findLoop(sub, end - 1, start - 1, -1);
        }
        int rfind(StringView sub, int start) const { return rfind(sub, start, length()); }
        int rfind(StringView sub) const { return rfind(sub, 0); }

        friend String operator+(StringView a, StringView b);

        String string() const;
        String capitalize() const;
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

        explicit operator bool() const { return !empty(); }
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
