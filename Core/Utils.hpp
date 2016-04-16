#pragma once

#include "span.h"
#include "string_span.h"

#include <vector>
#include <string>
#include <type_traits>

using gsl::span;

namespace xor
{
    // Pointer wrapper that becomes nullptr when moved from.
    // Helpful for concisely implementing Movable types.
    template <typename T>
    struct MovingPtr
    {
        T p = nullptr;

        static_assert(std::is_pointer<T>::value, "T must be a pointer type.");

        MovingPtr(T p = nullptr) : p(p) {}
        ~MovingPtr() {}

        MovingPtr(const MovingPtr &) = delete;
        MovingPtr &operator=(const MovingPtr &) = delete;

        MovingPtr(MovingPtr &&m)
            : p(m.p)
        {
            m.p = nullptr;
        }

        MovingPtr &operator=(MovingPtr &&m)
        {
            if (this != &m)
            {
                p = m.p;
                m.p = nullptr;
            }
            return *this;
        }

        explicit operator bool() const { return p; }
        operator T() { return p; }
        operator const T() const { return p; }
    };

    template <typename T>
    size_t size(const T &t)
    {
        using std::begin;
        using std::end;
        return end(t) - begin(t);
    }

    template <typename T>
    size_t sizeBytes(const T &t)
    {
        return ::xor::size(t) * sizeof(t[0]);
    }

    template <typename T>
    void zero(T &t)
    {
        memset(&t, 0, sizeof(t));
    }

    class Timer
    {
        double period;
        uint64_t start;
    public:
        Timer();
        double seconds() const;
    };

    // TODO: Move these elsewhere
    std::vector<std::string> listFiles(const std::string &path, const std::string &pattern = "*");
    std::vector<std::string> searchFiles(const std::string &path, const std::string &pattern);
    std::string replaceAll(std::string s, const std::string &replacedString, const std::string &replaceWith);
    std::vector<std::string> tokenize(const std::string &s, const std::string &delimiters);
    std::vector<std::string> splitPath(const std::string &path);

    std::string fileOpenDialog(const std::string &description, const std::string &pattern);
    std::string fileSaveDialog(const std::string &description, const std::string &pattern);
    std::string absolutePath(const std::string &path);

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

