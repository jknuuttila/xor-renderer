#pragma once

#include "OS.hpp"

#include <vector>
#include <string>
#include <memory>
#include <initializer_list>
#include <type_traits>

namespace xor
{
    class String;

    using uint = uint32_t;
    using ll   = long long;
    using llu  = unsigned long long;

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

    template <typename T, typename Deleter>
    auto raiiPtr(T *t, Deleter &&deleter)
    {
        return std::unique_ptr<T, Deleter>(t, deleter);
    }

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
        constexpr static HANDLE InvalidHandleValue = INVALID_HANDLE_VALUE;
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
        float secondsF() const { return static_cast<float>(seconds()); }

        double milliseconds() const { return seconds() * 1000.0; }
        float millisecondsF() const { return secondsF() * 1000.0f; }
    };

    template <typename T>
    class Span
    {
        T *m_begin = nullptr;
        T *m_end   = nullptr;
    public:
        Span() = default;

        template <typename U>
        Span(U &&u, decltype(*std::begin(u)) * = nullptr) : Span(std::begin(u), std::end(u)) {}

        Span(std::initializer_list<T> init) : Span(std::begin(init), std::end(init)) {}

        template <typename Iter>
        Span(Iter begin, Iter end)
            : m_begin(begin == end ? nullptr : std::addressof(*begin))
            , m_end(m_begin + (end - begin))
        {}

        Span(T *ptr, size_t size)
            : m_begin(ptr)
            , m_end(ptr + size)
        {}

        bool empty() const { return m_begin == m_end; }
        size_t size() const { return m_end - m_begin; }
        size_t sizeBytes() const { return size() * sizeof(T); }

        T *begin() { return m_begin; }
        T *end()   { return m_end; }
        const T *begin() const { return m_begin; }
        const T *end()   const { return m_end; }

        T *data() { return begin(); }
        const T *data() const { return begin(); }

        T &operator[](size_t i) { return m_begin[i]; }
        const T &operator[](size_t i) const { return m_begin[i]; }

        Span<T> operator()(int64_t begin, int64_t end)
        {
            if (end < 0) end += size();
            return Span<T>(m_begin + begin, m_begin + end);
        }
        Span<const T> operator()(int64_t begin, int64_t end) const
        {
            if (end < 0) end += size();
            return Span<const T>(m_begin + begin, m_begin + end);
        }
        Span<T> operator()(int64_t begin) { return operator()(begin, size()); }
        Span<const T> operator()(int64_t begin) const { return operator()(begin, size()); }
    };

    template <typename T>
    auto makeSpan(T *ptr, size_t size)
    {
        return Span<T>(ptr, size);
    }

    template <typename T>
    auto makeConstSpan(T *ptr, size_t size)
    {
        return Span<const T>(ptr, size);
    }

    template <typename T>
    auto asSpan(T &&t)
    {
        return Span<std::remove_reference_t<decltype(*std::begin(t))>>(std::forward<T>(t));
    }

    template <typename T>
    auto asConstSpan(T &&t)
    {
        return Span<const std::remove_reference_t<decltype(*std::begin(t))>>(std::forward<T>(t));
    }

    template <typename T>
    Span<uint8_t> asRWBytes(T &&t)
    {
        auto begin = reinterpret_cast<uint8_t *>(asSpan(std::forward<T>(t)).data());
        return Span<uint8_t>(begin, sizeBytes(t));
    }

    template <typename T>
    Span<const uint8_t> asBytes(const T &t)
    {
        auto begin = reinterpret_cast<const uint8_t *>(asSpan(t).data());
        return Span<const uint8_t>(begin, sizeBytes(t));
    }

    template <typename T, typename S>
    Span<T> reinterpretSpan(S &&s)
    {
        auto begin = reinterpret_cast<T *>(asSpan(s).data());
        return Span<T>(begin, sizeBytes(s) / sizeof(T));
    }

    template <typename T, typename S>
    Span<const T> reinterpretSpan(const S &s)
    {
        auto begin = reinterpret_cast<const T *>(asConstSpan(s).data());
        return Span<const T>(begin, sizeBytes(s) / sizeof(T));
    }

    template <typename T>
    class DynamicBuffer
    {
        static_assert(std::is_pod<T>::value,
                      "DynamicBuffer only supports POD types");

        std::unique_ptr<T[]> m_data = nullptr;
        size_t               m_size = 0;
    public:
        DynamicBuffer() = default;

        DynamicBuffer(size_t size)
        {
            resize(size);
        }

        DynamicBuffer(size_t size, T value)
        {
            resize(size, value);
        }

        bool empty() const
        {
            return m_size == 0;
        }

        explicit operator bool() const
        {
            return !!m_size;
        }

        size_t size() const
        {
            return m_size;
        }

        size_t sizeBytes() const
        {
            return m_size * sizeof(T);
        }

        void clear()
        {
            resize(0);
        }

        void resize(size_t size)
        {
            m_size = size;

            if (size)
                m_data = std::unique_ptr<T[]>(new T[size]);
            else
                m_data = nullptr;
        }

        void resize(size_t size, T value)
        {
            resize(size);
            fill(value);
        }

        void fill(T value)
        {
            if (sizeof(T) == 1)
            {
                memset(m_data.get(), value, size);
            }
            else
            {
                for (size_t i = 0; i < size; ++i)
                    m_data[i] = value;
            }
        }

        T *begin() { return m_data.get(); }
        T *end()   { return begin() + m_size; }
        T *data()  { return begin(); } 
        const T *begin() const { return m_data.get(); }
        const T *end()   const { return begin() + m_size; }
        const T *data()  const { return begin(); } 

        T &operator[](size_t i) { return m_data[i]; }
        const T &operator[](size_t i) const { return m_data[i]; }
    };

    String toString(bool b);
    String toString(uint u);
    String toString(int i);
    String toString(float f);
    String toString(double d);
}

