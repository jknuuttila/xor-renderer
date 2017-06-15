#pragma once

#include "OS.hpp"
#include "Utils.hpp"
#include "Error.hpp"
#include "Hash.hpp"

#include <string>
#include <vector>
#include <filesystem>

#include <cstdlib>
#include <cstring>
#include <cstdarg>

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

    template <size_t N>
    inline size_t stringLength(const char (&stringLiteral)[N])
    {
        return N - 1;
    }
    inline size_t stringLength(const char *str)
    {
        return std::strlen(str);
    }
    template <typename T>
    inline size_t stringLength(const T &t)
    {
        return t.size();
    }

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

        static int lastMatchableEnd(StringView sub, int end)
        {
            return end + 1 - sub.length();
        }

        int findLoop(StringView sub, int start, int end, int inc) const
        {
            int L = sub.length();
            if (L == 0)
                return -1;

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
        {
            if (!str)
            {
                m_begin = nullptr;
                m_end   = nullptr;
            }
            else
            {
                m_begin = str;
                m_end   = str + std::strlen(str);
            }
        }

        StringView(const char *start, const char *end)
            : m_begin(start)
            , m_end(end)
        {}

        StringView(Span<const char> chars)
            : m_begin(chars.data())
            , m_end(chars.data() + chars.size())
        {}

        explicit StringView(Span<const uint8_t> chars)
            : m_begin(reinterpret_cast<const char *>(chars.data()))
            , m_end(reinterpret_cast<const char *>(chars.data()) + chars.size())
        {}

        StringView(const std::string &str)
            : m_begin(str.data())
            , m_end(m_begin + str.length())
        {}

        inline StringView(const String &str);

        explicit operator bool() const { return m_begin != m_end; }
        bool empty() const { return m_begin == m_end; }

        size_t size() const { return m_end - m_begin; }
        int length() const { return static_cast<int>(size()); }

        const char *data()  const { return m_begin; }
        const char *begin() const { return m_begin; }
        const char *end()   const { return m_end; }

        const char &operator[](size_t i) const { return m_begin[i]; }

        String str() const;
        std::string stdString() const;
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

        friend int compare(StringView a, StringView b)
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

        StringView slice(int start, int end) const
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

        StringView operator()(int start, int end) const
        {
            return slice(start, end);
        }

        int find(StringView sub, int start, int end) const
        {
            start = idx(start);
            end   = idx(end);

            if (end - start < sub.length())
                return -1;
            else
                end = lastMatchableEnd(sub, end);

            return findLoop(sub, start, end, 1);
        }
        int find(StringView sub, int start) const { return find(sub, start, length()); }
        int find(StringView sub) const { return find(sub, 0); }

        bool contains(StringView sub) const
        {
            return find(sub) >= 0;
        }

        bool contains(char c) const
        {
            for (char cc : *this)
            {
                if (cc == c)
                    return true;
            }
            return false;
        }

        int count(StringView sub, int start, int end) const
        {
            start = idx(start);
            end   = idx(end);

            if (end - start < sub.length())
                return 0;
            else
                end = lastMatchableEnd(sub, end);

            int amount = 0;
            for (;;)
            {
                int found = findLoop(sub, start, end, 1);
                if (found < 0)
                {
                    return amount;
                }
                else
                {
                    start = found + 1;
                    ++amount;
                }
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
                end = lastMatchableEnd(sub, end);

            return findLoop(sub, end - 1, start - 1, -1);
        }
        int rfind(StringView sub, int start) const { return rfind(sub, start, length()); }
        int rfind(StringView sub) const { return rfind(sub, 0); }

        bool startsWith(StringView prefix) const
        {
            if (prefix.length() > length()) return false;
            return memcmp(data(), prefix.data(), prefix.length()) == 0;
        }

        bool endsWith(StringView suffix) const
        {
            if (suffix.length() > length()) return false;
            return memcmp(end() - suffix.length(), suffix.data(), suffix.length()) == 0;
        }

        friend String operator+(StringView a, StringView b);

        String capitalize() const;
        String lower() const;

        template <typename F>
        void splitForEach(F &&f, StringView separators = Whitespace, int maxSplit = -1) const
        {
            if (maxSplit < 0) maxSplit = -1;

            int len        = length();
            int splitStart = 0;
            int splitEnd   = 0;

            while (splitEnd < len)
            {
                if (maxSplit != 0 &&
                    separators.contains(this->operator[](splitEnd)))
                {
                    --maxSplit;
                    f(slice(splitStart, splitEnd));
                    ++splitEnd;
                    splitStart = splitEnd;
                }
                else
                {
                    ++splitEnd;
                }
            }

            if (splitStart != splitEnd)
            {
                f(slice(splitStart, splitEnd));
            }
        }

        std::vector<String> split(StringView separators = Whitespace, int maxSplit = -1) const;
        std::vector<String> splitNonEmpty(StringView separators = Whitespace, int maxSplit = -1) const;
        std::vector<String> lines() const;

        String strip(StringView separators = Whitespace,
                     bool leftStrip = true,
                     bool rightStrip = true) const;
        String swapCase() const;
        String upper() const;
        String leftJustify(int width, char filler = ' ') const;
        String rightJustify(int width, char filler = ' ') const;
        String center(int width, char filler = ' ') const;
        String replace(StringView old, StringView replacement, int maxReplace = -1) const;
        String replace(int start, int end, StringView replacement) const;
        String repeat(uint count) const;
    };

    class String : public StringView
    {
        friend class StringView;
        std::string m_str;

        void updateView() { static_cast<StringView &>(*this) = m_str; }
    public:
        String() = default;
        String(const char *str)
            : m_str(str ? str : "")
        {
            updateView();
        }
        String(const char *str, size_t length)
            : m_str(str, length)
            , StringView(m_str)
        {
            updateView();
        }

        template <typename Iter>
        String(Iter begin, Iter end)
            : m_str(begin, end)
        {
            updateView();
        }
        String(const std::string &str)
            : m_str(str)
        {
            updateView();
        }

        String(std::string &&str)
            : m_str(std::move(str))
            , StringView(m_str)
        {
            updateView();
        }
        String(StringView str)
            : m_str(str.begin(), str.end())
            , StringView(m_str)
        {
            updateView();
        }
        explicit String(const std::wstring &wstr);
        explicit String(const wchar_t *wstr);

        String(const std::experimental::filesystem::path &path)
            : String(path.c_str())
        {}

        String(const String &s)
            : m_str(s.m_str)
            , StringView(m_str)
        {
            updateView();
        }
        String(String &&s)
            : m_str(std::move(s.m_str))
            , StringView(m_str)
        {
            updateView();
        }
        String &operator=(const String &s)
        {
            if (this != &s)
            {
                m_str = s.m_str;
                updateView();
            }
            return *this;
        }
        String &operator=(String &&s)
        {
            if (this != &s)
            {
                m_str = std::move(s.m_str);
                updateView();
            }
            return *this;
        }
        String &operator=(StringView s)
        {
            if (this != &s)
            {
                m_str = s.stdString();
                updateView();
            }
            return *this;
        }
        String &operator=(const char *s)
        {
            return operator=(String(s));
        }

        static String format(const char *fmt, ...);
        static String vformat(const char *fmt, va_list ap);

        const char *cStr() const { return m_str.c_str(); }
        std::wstring wideStr() const;
        std::experimental::filesystem::path path() const
        {
            return std::experimental::filesystem::path(wideStr());
        }

        template <typename SpanOfStrings>
        static String join(const SpanOfStrings &strings, StringView separator = " ")
        {
            size_t length = 0;
            for (auto &s : strings) length += stringLength(s) + separator.length();

            std::string result;
            result.reserve(length + 1);

            using std::begin;
            using std::end;
            bool first = true;
            for (auto &s : strings)
            {
                if (!first)
                    result.append(separator.begin(), separator.end());

                result.append(begin(s), end(s));
                first = false;
            }

            return String(std::move(result));
        }
    };

    inline StringView::StringView(const String &str)
        : m_begin(str.data())
        , m_end(m_begin + str.length())
    {}
}

namespace std
{
    template <>
    struct hash<xor::String>
    {
        size_t operator()(const xor::String &s) const
        {
            return xor::hashBytes(s.begin(), s.end());
        }
    };

    template <>
    struct hash<xor::StringView>
    {
        size_t operator()(const xor::StringView &s) const
        {
            return xor::hashBytes(s.begin(), s.end());
        }
    };
}
