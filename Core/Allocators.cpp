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

        // Put the offsets in back-to-front, so offset 0 is the first to
        // get allocated.
        for (int64_t i = ssize - 1; i >= 0; --i)
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

    size_t OffsetRing::freeSpace() const
    {
        if (m_full)
            return 0;

        int64_t used = m_tail - m_head;
        if (used < 0)
            used += m_size;

        return static_cast<size_t>(m_size - used);
    }

    int64_t OffsetRing::allocate()
    {
        XOR_ASSERT(m_size > 0, "Attempted to allocate from an invalid ring.");

        if (m_full)
            return -1;

        int64_t offset = m_tail;

        ++m_tail;
        XOR_ASSERT(m_tail <= m_size, "Tail out of bounds.");
        if (m_tail == m_size)
            m_tail = 0;

        m_full = m_tail == m_head;

        return offset;
    }

    int64_t OffsetRing::allocateContiguous(size_t amount)
    {
        XOR_ASSERT(m_size > 0, "Attempted to allocate from an invalid ring.");
        XOR_ASSERT(amount > 0, "Attempted to allocate zero elements.");

        int64_t iAmount = static_cast<int64_t>(amount);

        if (m_full || iAmount > m_size)
            return -1;

        int64_t offset;

        if (m_tail < m_head)
        {
            // All free space is between tail and head.
            int64_t left = m_head - m_tail;

            if (left < iAmount)
                return -1;

            offset = m_tail;
            m_tail += iAmount;
        }
        else
        {
            // Free space is split in two: between tail and buffer end,
            // and between buffer start and head.

            int64_t leftUntilEnd = m_size - m_tail;

            if (leftUntilEnd < iAmount)
            {
                // Not enough space between tail and end, what about
                // buffer start and head?
                int64_t leftUntilHead = m_head;
                if (leftUntilHead < iAmount)
                    return -1;

                // Move tail directly to offset iAmount, which essentially
                // allocates all space between tail and end, and
                // iAmount elements from buffer start.
                offset = 0;
                m_tail = iAmount;
            }
            else
            {
                // There is enough space between tail and end, allocate
                // from there.
                offset = m_tail;
                m_tail += iAmount;
                XOR_ASSERT(m_tail <= m_size, "Tail out of bounds.");
                if (m_tail == m_size)
                    m_tail = 0;
            }
        }

        m_full = m_tail == m_head;
        return offset;
    }

    void OffsetRing::releaseEnd(int64_t onePastLastOffset)
    {
        XOR_ASSERT(!empty(), "Attempted to release when the ring is empty.");
#if XOR_ASSERTIONS
        // In order to make sense, the one-past-last should either be equal to
        // tail (ring was just emptied), or it should lie within the currently
        // allocated region.
        if (m_tail > m_head)
        {
            // All allocated space is between head and tail, so one-past-last
            // must lie within that region.
            XOR_ASSERT(onePastLastOffset <= m_tail,
                       "Attempted to release unallocated elements.");
        }
        else
        {
            // Allocated space wraps off the end of the buffer, so one-past-last
            // cannot lie between tail and head.
            XOR_ASSERT(onePastLastOffset <= m_tail ||
                       onePastLastOffset > m_head,
                       "Attempted to release unallocated elements.");
        }
#endif

        XOR_ASSERT(onePastLastOffset >= 0 && onePastLastOffset <= m_size,
                   "Released range out of bounds.");
        m_head = onePastLastOffset;
        // We just released something, so the ring cannot be full.
        m_full = false;
    }
}
