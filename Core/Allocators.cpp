#include "Allocators.hpp"
#include "Error.hpp"

#include <numeric>

namespace xor
{
    OffsetPool::OffsetPool(size_t size)
    {
        XOR_ASSERT(size <= static_cast<size_t>(std::numeric_limits<int64_t>::max()),
                   "Size must be representable with a signed 64-integer.");

        m_size = size;
        m_freeOffsets.reserve(size);
        auto ssize = static_cast<int64_t>(size);
        for (int64_t i = 0; i < ssize; ++i)
            m_freeOffsets.emplace_back(i);
    }

    bool OffsetPool::empty() const
    {
        return m_freeOffsets.empty();
    }

    bool OffsetPool::full() const
    {
        return spaceLeft() == size();
    }

    size_t OffsetPool::size() const
    {
        return m_size;
    }

    size_t OffsetPool::spaceLeft() const
    {
        return m_freeOffsets.size();
    }

    int64_t OffsetPool::allocate()
    {
        if (empty())
            return -1;

        int64_t offset = m_freeOffsets.back();
        m_freeOffsets.pop_back();
        return offset;
    }

    void OffsetPool::release(int64_t offset)
    {
        XOR_ASSERT(offset >= 0 && static_cast<size_t>(offset) < m_size,
                   "Attempted to release an invalid offset.");
        XOR_ASSERT(!full(),
                   "Attempted to release when pool is already full.");

        m_freeOffsets.emplace_back(offset);
    }
}
