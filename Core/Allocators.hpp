#pragma once

#include "Utils.hpp"
#include "Error.hpp"

#include <vector>
#include <algorithm>

namespace xor
{
    // Simple pool allocator that manages abstract offsets.
    class OffsetPoolAllocator
    {
        size_t               m_size = 0;
        std::vector<int64_t> m_freeOffsets;
    public:
        OffsetPoolAllocator(size_t size);

        bool empty() const;
        bool full() const;

        size_t size() const;
        size_t spaceLeft() const;

        int64_t allocate();
        void release(int64_t offset);
    };

    // Object pool allocator using a simple std::vector.
    template <typename T>
    class PoolAllocator
    {
        size_t         m_size = 0;
        std::vector<T> m_objects;
    public:
        PoolAllocator(size_t size)
            : m_size(size)
            , m_objects(size)
        {}

        // Allow access to objects, for e.g. initialization.
        span<T> span() { return m_objects; }

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
}

