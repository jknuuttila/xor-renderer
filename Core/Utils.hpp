#pragma once

#include "OS.hpp"

#include "span.h"
#include "string_span.h"

#include <vector>
#include <string>
#include <type_traits>

namespace xor
{
    using namespace gsl;

    // TODO: move this elsewhere
    using ll   = long long;
    using ull  = unsigned long long;

    // Pointer wrapper that becomes nullptr when moved from.
    // Helpful for concisely implementing Movable types.
    template <typename T, T NullValue = nullptr>
    struct MovingPtr
    {
        T p = NullValue;

        static_assert(std::is_pointer<T>::value, "T must be a pointer type.");

        MovingPtr(T p = NullValue) : p(p) {}

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

        explicit operator bool() const { return p != nullptr && p != NullValue; }

        T get() { return p; }
        const T get() const { return p; }

        operator T() { return get(); }
        operator const T() const { return get(); }

        T *operator->() { return get(); }
        const T *operator->() const { return get(); }
    };

    // Assigns monotonically increasing consecutive non-negative sequence numbers,
    // and keeps track of which ones have completed. Sequence numbers
    // can complete in arbitrary order.
    using SeqNum = int64_t;
    static const SeqNum InvalidSeqNum = -1;
    class SequenceTracker
    {
        int64_t               m_next = 0;
        int64_t               m_uncompletedBase = 0;
        std::vector<uint64_t> m_uncompletedBits;

        struct Bit
        {
            int64_t qword;
            uint64_t mask;
        };

        Bit bit(SeqNum seqNum) const;
        int64_t lowestSetBit() const;
        void removeCompletedBits();
    public:
        SeqNum start();
        void complete(SeqNum seqNum);

        SeqNum newestStarted() const;
        SeqNum oldestUncompleted() const;
        bool hasCompleted(SeqNum seqNum) const;
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
            close();
        }

        explicit operator bool() const { return static_cast<bool>(m_handle); }
        HANDLE get() const { return m_handle; }

        HANDLE *outRef()
        {
            close();
            return &m_handle.p;
        }

        void close()
        {
            if (m_handle)
            {
                CloseHandle(m_handle.get());
                m_handle = InvalidHandleValue;
            }
        }
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
}

