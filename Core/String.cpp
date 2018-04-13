#include "String.hpp"
#include "Error.hpp"

#include <vector>
#include <cstdlib>
#include <locale>
#include <codecvt>
#include <cctype>

namespace Xor
{
    static std::wstring_convert<std::codecvt_utf8<wchar_t>> wConverter;

    String::String(const std::wstring &wstr)
        : m_str(wConverter.to_bytes(wstr))
    {
        updateView();
    }

    String::String(const wchar_t *wstr)
        : m_str(wConverter.to_bytes(wstr))
    {
        updateView();
    }

    String String::vformat(const char *fmt, va_list ap)
    {
        char buffer[1024];
        int size = vsnprintf(buffer, sizeof(buffer), fmt, ap);
        if (size > sizeof(buffer))
        {
            std::vector<char> largeBuffer(size);
            int wrote = vsnprintf(largeBuffer.data(), size, fmt, ap);
            XOR_ASSERT(size == wrote, "Unexpected amount of characters written.");
            return String(largeBuffer.data(), size - 1);
        }
        else
        {
            return String(buffer);
        }
    }

    String String::format(const char *fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        String s = vformat(fmt, ap);
        va_end(ap);
        return s;
    }

    std::wstring String::wideStr() const
    {
        return wConverter.from_bytes(m_str);
    }

    String StringView::str() const
    {
        return String(begin(), end());
    }

    std::string StringView::stdString() const
    {
        return std::string(begin(), end());
    }

    std::wstring StringView::wideStr() const
    {
        return wConverter.from_bytes(str().m_str);
    }

    String operator+(StringView a, StringView b)
    {
        std::string s(a.begin(), a.end());
        s.append(b.data(), b.size());
        return String(std::move(s));
    }

    String StringView::capitalize() const
    {
        auto s = stdString();
        s[0] = std::toupper(s[0]);
        return String(std::move(s));
    }

    String StringView::lower() const
    {
        auto s = stdString();
        for (char &c : s) c = std::tolower(c);
        return String(std::move(s));
    }

    std::vector<String> StringView::split(StringView separators, int maxSplit) const
    {
        std::vector<String> result;
        splitForEach([&] (StringView s)
        {
            result.emplace_back(s.str());
        }, separators, maxSplit);
        return result;
    }

    std::vector<String> StringView::splitNonEmpty(StringView separators, int maxSplit) const
    {
        std::vector<String> result;
        splitForEach([&] (StringView s)
        {
            if (!s.empty())
                result.emplace_back(s.str());
        }, separators, maxSplit);
        return result;
    }

    std::vector<String> StringView::lines() const
    {
        return replace("\r", "").split("\n");
    }

    String StringView::strip(StringView separators, bool leftStrip, bool rightStrip) const
    {
        size_t start = 0;
        size_t end   = size();

        if (leftStrip)
        {
            for (; start < end; ++start)
            {
                if (!separators.contains(operator[](start)))
                    break;
            }
        }

        if (rightStrip)
        {
            for (; end > start; --end)
            {
                if (!separators.contains(operator[](end - 1)))
                    break;
            }
        }

        return String(begin() + start, begin() + end);
    }

    String StringView::swapCase() const
    {
        auto s = stdString();
        for (char &c : s)
        {
            if (std::isupper(c))
                c = std::tolower(c);
            else
                c = std::toupper(c);
        }
        return String(std::move(s));
    }

    String StringView::upper() const
    {
        auto s = stdString();
        for (char &c : s) c = std::toupper(c);
        return String(std::move(s));
    }

    String StringView::leftJustify(int width, char filler) const
    {
        int len = std::max(width, length());
        std::string result;
        result.reserve(len);
        result.append(begin(), end());
        for (int i = length(); i < len; ++i)
            result.push_back(filler);
        return String(std::move(result));
    }

    String StringView::rightJustify(int width, char filler) const
    {
        int len = std::max(width, length());
        std::string result;
        result.reserve(len);
        for (int i = length(); i < len; ++i)
            result.push_back(filler);
        result.append(begin(), end());
        return String(std::move(result));
    }

    String StringView::center(int width, char filler) const
    {
        int len   = std::max(width, length());
        std::string result;
        result.reserve(len);
        int fill  = len - width;
        int left  = fill / 2;
        int right = fill - left;
        for (int i = 0; i < left; ++i) result.push_back(filler);
        result.append(begin(), end());
        for (int i = 0; i < right; ++i) result.push_back(filler);
        return String(std::move(result));
    }

    String StringView::replace(StringView old, StringView replacement, int maxReplace) const
    {
        int hits = count(old);

        if (hits == 0 || maxReplace == 0)
            return str();

        if (maxReplace < 0) maxReplace = -1;

        int diff = replacement.length() - old.length();
        int len  = length() + hits * diff;

        std::string result;
        result.reserve(len);

        int i = 0;
        while (i < length())
        {
            if (from(i).startsWith(old))
            {
                // If maxReplace is negative, we always replace
                if (maxReplace != 0)
                {
                    result.append(replacement.begin(),
                                  replacement.end());
                    --maxReplace;
                }
                else
                {
                    result.append(old.begin(),
                                  old.end());
                }

                i += old.length();
            }
            else
            {
                result.push_back(operator[](i));
                ++i;
            }
        }

        return String(std::move(result));
    }

    String StringView::replace(int start, int end, StringView replacement) const
    {
        start = idx(start);
        end   = idx(end);

        std::string result;
        result.reserve(length() + replacement.length() + start - end);
        result.insert(result.end(), begin(), begin() + start);
        result.insert(result.end(), replacement.begin(), replacement.end());
        result.insert(result.end(), begin() + end, this->end());
        return String(std::move(result));
    }

    String StringView::repeat(uint count) const
    {
        if (count == 0)
            return String();

        std::string result;
        std::string s = stdString();

        result.reserve(count * static_cast<uint>(length()));

        for (uint i = 0; i < count; ++i)
            result += s;

        return String(std::move(result));
    }
}
