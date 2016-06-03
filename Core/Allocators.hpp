#pragma once

#include "Utils.hpp"
#include "Error.hpp"

#include <vector>
#include <algorithm>
#include <functional>

namespace xor
{
    // Simple pool allocator that manages abstract offsets.
    class OffsetPool
    {
        size_t               m_size = 0;
        std::vector<int64_t> m_freeOffsets;
    public:
        OffsetPool() = default;
        OffsetPool(size_t size);

        bool empty() const;
        bool full() const;

        size_t size() const;
        size_t spaceLeft() const;

        int64_t allocate();
        void release(int64_t offset);
    };

    // Object pool allocator using a simple std::vector.
    template <typename T>
    class Pool
    {
        size_t         m_size = 0;
        std::vector<T> m_objects;
    public:
        Pool() = default;
        Pool(size_t size)
            : m_size(size)
            , m_objects(size)
        {}

        // Allow access to objects, for e.g. initialization.
        Span<T> span() { return m_objects; }

        bool empty() const       { return m_objects.empty(); }
        bool full() const        { return spaceLeft() == size(); }

        size_t size() const      { return m_size; }
        size_t spaceLeft() const { return m_objects.size(); }

        T allocate()
        {
            XOR_CHECK(!m_objects.empty(), "Ran out of space in the pool.");
            T object = std::move(m_objects.back());
            m_objects.pop_back();
            return object;
        }

        void release(T object)
        {
            m_objects.emplace_back(std::move(object));
            XOR_ASSERT(m_objects.size() <= m_size,
                       "Object count exceeds size, which is a bug.");
        }
    };

    template <typename T>
    class GrowingPool
    {
        std::vector<T> m_objects;
    public:
        template <typename MakeT>
        T allocate(MakeT &&make)
        {
            if (!m_objects.empty())
            {
                T t = std::move(m_objects.back());
                m_objects.pop_back();
                return t;
            }
            else
            {
                return make();
            }
        }

        void release(T object)
        {
            m_objects.emplace_back(std::move(object));
        }
    };
}

