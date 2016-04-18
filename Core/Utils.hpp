#pragma once

#include "span.h"
#include "string_span.h"

#include <vector>
#include <string>
#include <type_traits>

using gsl::span;

namespace xor
{
    // TODO: move this elsewhere
    using ull  = unsigned long long;

    // Pointer wrapper that becomes nullptr when moved from.
    // Helpful for concisely implementing Movable types.
    template <typename T, T NullValue = nullptr>
    struct MovingPtr
    {
        T p = NullValue;

        static_assert(std::is_pointer<T>::value, "T must be a pointer type.");

        MovingPtr(T p = NullValue) : p(p) {}
        ~MovingPtr() {}

        MovingPtr(const MovingPtr &) = delete;
        MovingPtr &operator=(const MovingPtr &) = delete;

        MovingPtr(MovingPtr &&m)
            : p(m.p)
        {
            m.p = NullValue;
        }

        MovingPtr &operator=(MovingPtr &&m)
        {
            if (this != &m)
            {
                p = m.p;
                m.p = NullValue;
            }
            return *this;
        }

        MovingPtr &operator=(std::nullptr_t)
        {
            p = NullValue;
            return *this;
        }

        explicit operator bool() const { return p != NullValue; }

        T get() { return p; }
        const T get() const { return p; }

        operator T() { return get(); }
        operator const T() const { return get(); }

        T *operator->() { return get(); }
        const T *operator->() const { return get(); }
    };

    // Assigns monotonically increasing consecutive sequence numbers,
    // and keeps track of which ones have completed. Sequence numbers
    // can complete in arbitrary order.
    class SequenceTracker
    {
        uint64_t              m_next = 0;
        uint64_t              m_uncompletedBase = 0;
        std::vector<uint64_t> m_uncompletedBits;

        struct Bit
        {
            int64_t qword;
            uint64_t mask;
        };

        Bit bit(uint64_t seqNum) const;
        int64_t lowestSetBit() const;
        void removeCompletedBits();
    public:
        uint64_t start();
        void complete(uint64_t seqNum);

        uint64_t oldestUncompleted() const;
        bool hasCompleted(uint64_t seqNum) const;
    };

    class Handle
    {
        static constexpr HANDLE InvalidHandleValue = INVALID_HANDLE_VALUE;
        MovingPtr<HANDLE, InvalidHandleValue> m_handle;
    public:
        Handle(HANDLE handle = INVALID_HANDLE_VALUE)
            : m_handle(handle)
        {}

        Handle(Handle &&) = default;
        Handle &operator=(Handle &&) = default;

        ~Handle()
        {
            if (m_handle)
                CloseHandle(m_handle.get());
        }

        explicit operator bool() const { return static_cast<bool>(m_handle); }
        HANDLE get() { return m_handle; }
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

    std::string fileOpenDialog(const std::string &description, const std::string &pattern);
    std::string fileSaveDialog(const std::string &description, const std::string &pattern);
    std::string absolutePath(const std::string &path);
    std::vector<std::string> splitPath(const std::string &path);
}

