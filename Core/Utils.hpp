#pragma once

#include "Core/OS.hpp"
#include "Core/Error.hpp"

#include <vector>
#include <string>
#include <memory>
#include <initializer_list>
#include <type_traits>
#include <algorithm>

namespace xor
{
    class String;

    using uint = uint32_t;
    using lld  = long long;
    using llu  = unsigned long long;

    template <typename T>
    struct IsPod
    {
        static const bool value =
            std::is_trivially_copyable<T>::value &&
            std::is_trivially_destructible<T>::value;
    };

    // A canonical empty struct for things like empty base optimizations.
    struct Empty {};
    static_assert(std::is_empty<Empty>::value, "Empty is not empty");

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

        T operator->() { return get(); }
        const T operator->() const { return get(); }
    };

    // As MovingPtr, but for arbitrary POD values
    template <typename T, T NullValue = T{}>
    struct MovingValue
    {
        static_assert(std::is_pod<T>::value, "MovingValue only supports POD types");

        T v = NullValue;

        MovingValue(T value = NullValue) : v(value) {}

        MovingValue(const MovingValue &) = delete;
        MovingValue &operator=(const MovingValue &) = delete;

        MovingValue(MovingValue &&mv)
            : v(mv.v)
        {
            mv.v = NullValue;
        }

        MovingValue &operator=(MovingValue &&mv)
        {
            v    = mv.v;
            mv.v = NullValue;
            return *this;
        }

        explicit operator bool() const { return v != NullValue; }

        operator T() const { return v; }
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
        MovingPtr<HANDLE, INVALID_HANDLE_VALUE> m_handle;
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
                m_handle = INVALID_HANDLE_VALUE;
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
        double period  = 0;
        uint64_t start = 0;
    public:
        Timer();
        void reset();

        double seconds() const;
        float secondsF() const { return static_cast<float>(seconds()); }

        double milliseconds() const { return seconds() * 1000.0; }
        float millisecondsF() const { return secondsF() * 1000.0f; }

        double bandwidthMB(size_t bytes) const
        {
            static const double MB = 1024 * 1024;
            return static_cast<double>(bytes) / MB / seconds();
        }
    };

    template <typename T>
    class Span
    {
        T *m_begin = nullptr;
        T *m_end   = nullptr;
    public:
        Span() = default;

        template <typename U>
        Span(U &&u) : Span(std::begin(u), std::end(u)) {}

        Span(std::initializer_list<T> init) : Span(init.begin(), init.end()) {}

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
            return Span<T>(std::min(m_begin + begin, m_end), std::min(m_begin + end, m_end));
        }
        Span<const T> operator()(int64_t begin, int64_t end) const
        {
            if (end < 0) end += size();
            return Span<const T>(std::min(m_begin + begin, m_end), std::min(m_begin + end, m_end));
        }
        Span<T> operator()(int64_t begin) { return operator()(begin, size()); }
        Span<const T> operator()(int64_t begin) const { return operator()(begin, size()); }
    };

    template <typename T>
    using ElementType = std::remove_reference_t<decltype(std::declval<T>()[0])>;

    template <typename T, typename... Ts>
    auto array(T && t, Ts &&... ts)
    {
        return std::array<std::decay_t<T>, sizeof...(Ts) + 1> { t, ts... };
    }

    template <typename T>
    auto makeSpan(T *ptr, size_t size = 1)
    {
        return Span<T>(ptr, size);
    }

    template <typename T>
    auto makeConstSpan(T *ptr, size_t size = 1)
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

    // Allocate a dynamic amount of POD memory cheaply,
    // without requiring initialization.
    template <typename T>
    class DynamicBuffer
    {
        static_assert(std::is_pod<T>::value,
                      "DynamicBuffer only supports POD types");

        std::unique_ptr<T[]> m_data     = nullptr;
        size_t               m_size     = 0;
        size_t               m_capacity = 0;
    public:
        DynamicBuffer() = default;

        DynamicBuffer(size_t size)
        {
            resize(size);
        }

        DynamicBuffer(size_t size, T value)
        {
            resize(size);
            fill(value);
        }

        DynamicBuffer(Span<const T> data)
        {
            resize(data.size());
            memcpy(m_data.get(), data.data(), sizeBytes());
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
            resize(0, false);
        }

        void release()
        {
            resize(0, true);
        }

        void reserve(size_t capacity)
        {
            if (capacity > m_capacity)
            {
                auto actualSize = m_size;
                resize(capacity);
                resize(actualSize);
            }
        }

        void resize(size_t size, bool shrink = false)
        {
            if (size == m_size)
                return;

            if (size || !shrink)
            {
                // If we are reducing size and can shrink, do so.
                if (size < m_size && !shrink)
                {
                    m_size = size;
                    return;
                }

                // Otherwise, if we are reducing size we
                // allocate a smaller array.
                if (size < m_size)
                {
                    m_capacity = size;
                }
                // If we are growing, but the capacity is enough,
                // just adjust the size.
                else if (size <= m_capacity)
                {
                    m_size = size;
                    return;
                }
                // Finally, if there is not enough capacity, get
                // more space.
                else
                {
                    auto capacity = m_capacity * 3 / 2;
                    capacity = std::max(capacity, size);
                    m_capacity = capacity;
                }

                auto copyElements = std::min(size, m_size);
                auto copyBytes    = copyElements * sizeof(T);

                auto newData = std::unique_ptr<T[]>(new T[m_capacity]);

                memcpy(newData.get(), m_data.get(), copyBytes);

                m_data = std::move(newData);
            }
            else
            {
                m_data = nullptr;
            }

            m_size = size;
        }

        void fill(T value)
        {
            if (sizeof(T) == 1)
            {
                memset(m_data.get(), value, m_size);
            }
            else
            {
                for (size_t i = 0; i < m_size; ++i)
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

    // Allocate a dynamic amount of POD memory using VirtualAlloc,
    // so resizing is cheap as it doesn't have to copy. Maximum
    // size needs to be specified up front.
    template <typename T>
    class VirtualBuffer
    {
        static_assert(std::is_pod<T>::value,
                      "VirtualBuffer only supports POD types");

        void allocate(size_t maximumSize)
        {
            release();

            m_data = reinterpret_cast<T *>(
                VirtualAlloc(nullptr,
                             maximumSize,
                             MEM_RESERVE,
                             PAGE_READWRITE));
            XOR_CHECK_LAST_ERROR(!!m_data);

            m_maximumSize = maximumSize;
        }

        void release()
        {
            if (m_data)
            {
                VirtualFree(m_data.get(), 0, MEM_RELEASE);
                m_data = nullptr;
            }
        }

        MovingPtr<T *> m_data        = nullptr;
        size_t         m_size        = 0;
        size_t         m_maximumSize = 0;

    public:
        VirtualBuffer() = default;
        ~VirtualBuffer() { release(); }

        VirtualBuffer(size_t maximumSize, size_t size = 0)
        {
            allocate(maximumSize);
            resize(size);
        }

        VirtualBuffer(size_t maximumSize, size_t size, T value)
        {
            allocate(maximumSize);
            resize(size);
            fill(value);
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

        void reserve(size_t) {}

        void resize(size_t size)
        {
            if (size == m_size)
                return;

            XOR_CHECK(size <= m_maximumSize, "Cannot exceed the original maximum size");

            if (size > m_size)
            {
                // Growing the area, commit more pages
                auto start = end();
                auto bytes = size * sizeof(T) - sizeBytes();
                auto retval = VirtualAlloc(start,
                                           bytes,
                                           MEM_COMMIT,
                                           PAGE_READWRITE);
                XOR_CHECK_LAST_ERROR(!!retval);
            }
            else
            {
                // Shrinking the area, release some pages.
                auto start = begin() + size;
                auto bytes = end() - start;
                auto retval = VirtualFree(start,
                                          bytes,
                                          MEM_DECOMMIT);
                XOR_CHECK_LAST_ERROR(!!retval);
            }

            m_size = size;
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

    // Store a pointer using a 32-bit difference to the address
    // of the object. Is POD and absolute position independent,
    // so can be stored to disk and can be
    // used directly inside e.g. memory-mapped or loaded files.
    template <typename T, uint DiscardLowBits = 0>
    class DiffPtr
    {
        int32_t diff = 0;
    public:
        DiffPtr() = default;
        DiffPtr(T *p)
        {
            *this = p;
        }

        explicit operator bool() const { return diff != 0; }

        DiffPtr &operator=(T *p)
        {
            intptr_t d =
                reinterpret_cast<intptr_t>(p) -
                reinterpret_cast<intptr_t>(this);
            diff = static_cast<int32_t>(d >> DiscardLowBits);
            XOR_ASSERT(get() == p, "Cannot encode pointer in the available space");
            return *this;
        }

        const T *get() const
        {
            return reinterpret_cast<const T *>(
                reinterpret_cast<intptr_t>(this) +
                (diff << DiscardLowBits));
        }

        T *get()
        {
            return reinterpret_cast<T *>(
                reinterpret_cast<intptr_t>(this) +
                (diff << DiscardLowBits));
        }

        const T *operator->() const { return get(); }
        T *operator->() { return get(); }
        const T &operator*() const { return *get(); }
        T &operator*() { return *get(); }
    };

    String toString(bool b);
    String toString(uint u);
    String toString(int i);
    String toString(float f);
    String toString(double d);

    template <typename F>
    class ScopeGuard
    {
        MovingPtr<F *> m_f;
    public:
        ScopeGuard(F &&f) : m_f(&f) {}
        ~ScopeGuard()
        {
            if (m_f)
                (*m_f)();
        }
        ScopeGuard(ScopeGuard &&) = default;
        ScopeGuard &operator=(ScopeGuard &&) = default;

        void cancel() { m_f = nullptr; }
    };

    template <typename F>
    auto scopeGuard(F &&f) { return ScopeGuard<F>(std::forward<F>(f)); }

    template <typename S>
    void sort(S &&span)
    {
        using std::begin;
        using std::end;
        std::sort(begin(span), end(span));
    }
}

#define XOR_CONCAT2(a, b) a ## b
#define XOR_CONCAT(a, b) XOR_CONCAT2(a, b)

