#include "Allocators.hpp"
#include "Error.hpp"
#include "Math.hpp"

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

    int64_t OffsetRing::allocateContiguous(size_t amount, size_t alignment)
    {
        XOR_ASSERT(m_size > 0, "Attempted to allocate from an invalid ring.");
        XOR_ASSERT(amount > 0, "Attempted to allocate zero elements.");
        XOR_ASSERT(alignment > 0, "Attempted to allocate with zero alignment.");

        int64_t iAmount = static_cast<int64_t>(amount);

        if (m_full || iAmount > m_size)
            return -1;

        int64_t offset;

        int64_t alignedTail = alignTo(m_tail, static_cast<int64_t>(alignment));

        if (m_tail < m_head)
        {
            // All free space is between tail and head.
            int64_t left = m_head - alignedTail;

            if (left < iAmount)
                return -1;

            offset = alignedTail;
            m_tail = alignedTail + iAmount;
        }
        else
        {
            // Free space is split in two: between tail and buffer end,
            // and between buffer start and head.

            int64_t leftUntilEnd = m_size - alignedTail;

            if (leftUntilEnd < iAmount)
            {
                // Not enough space between tail and end, what about
                // buffer start and head?
                int64_t leftUntilHead = m_head;
                if (leftUntilHead < iAmount)
                    return -1;

                // Move tail directly to offset iAmount, which essentially
                // allocates all space between tail and end, and
                // iAmount elements from buffer start. Also, offset = 0
                // is always aligned.
                offset = 0;
                m_tail = iAmount;
            }
            else
            {
                // There is enough space between tail and end, allocate
                // from there.
                offset = alignedTail;
                m_tail = alignedTail + iAmount;
                XOR_ASSERT(m_tail <= m_size, "Tail out of bounds.");
                if (m_tail == m_size)
                    m_tail = 0;
            }
        }

        m_full = m_tail == m_head;
        return offset;
    }

    int64_t OffsetRing::allocateContiguous(size_t amount)
    {
        return allocateContiguous(amount, 1);
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

    OffsetHeap::OffsetHeap(size_t size, uint minimumAlignment)
        : m_size(static_cast<int64_t>(size))
        , m_minAlignment(minimumAlignment)
    {
        // Insert the entire free space into the allocator
        // by "releasing" it.
        Block entireHeap;
        entireHeap.begin = 0;
        entireHeap.end   = m_size;
        release(entireHeap);
    }

    OffsetHeap::SizeAlignment OffsetHeap::encodeSizeAlignment(size_t size, uint alignment)
    {
        static const uint64_t AlignmentMask = (1ULL << AlignmentBits) - 1;
        SizeAlignment sa = size << AlignmentBits;
        sa |= static_cast<uint64_t>(alignment) & AlignmentMask;
        return sa;
    }

    Block OffsetHeap::allocate(size_t size)
    {
        return allocate(size, m_minAlignment);
    }

    Block OffsetHeap::allocate(size_t size, uint alignment)
    {
        SizeAlignment key = encodeSizeAlignment(size, alignment);

        auto it  = m_sizeBins.lower_bound(key);
        auto end = m_sizeBins.end();

        for (;;)
        {
            if (it == end)
            {
                // We didn't find any blocks that could fit
                // the given size and alignment requirements.
                return Block();
            }

            // Check if the size class can actually hold this
            // allocation. Even if the size is enough, alignment
            // might not match. Misaligned size classes will also
            // do, if they are large enough so they can be adjusted.

            if (canFit(it->first, size, alignment))
            {
                // We found a suitable block!
                auto blockSize      = decodeSize(it->first);
                auto blockAlignment = decodeAlignment(it->first);

                // Find the actual offset for it.

                auto &sizeBin = it->second;
                XOR_ASSERT(!sizeBin.freeOffsets.empty(),
                           "Size bins should always be non-empty.");
                int64_t offset = sizeBin.freeOffsets.top();
                sizeBin.freeOffsets.pop();
                int64_t blockEnd = offset + static_cast<int64_t>(blockSize);

                // If we just emptied the size bin, remove it from the
                // map.
                if (sizeBin.freeOffsets.empty())
                    m_sizeBins.erase(it);

                // Check if we need to chop off extra bits first.

                if (blockSize == size)
                {
                    // The block is an exact match, so no leftovers.
                    Block b;
                    b.begin = offset;
                    b.end   = b.begin + size;
                    allocateBlock(b);

                    return b;
                }
                else
                {
                    // First, find the first properly aligned offset
                    // from within the block.
                    Block b;
                    b.begin = roundUpToMultiple<int64_t>(offset, alignment);
                    b.end   = b.begin + size;
                    allocateBlock(b);

                    // Was there space left in the beginning because
                    // of alignment?
                    if (b.begin > offset)
                    {
                        Block prefix;
                        prefix.begin = offset;
                        prefix.end   = b.begin;
                        release(prefix);
                    }

                    // Was there space left in the end because the
                    // block was larger?
                    if (b.end < blockEnd)
                    {
                        Block suffix;
                        suffix.begin = b.end;
                        suffix.end   = blockEnd;
                        release(suffix);
                    }

                    return b;
                }
            }
            else
            {
                // Advance to the next larger size class.
                ++it;
            }
        }
    }

    void OffsetHeap::release(Block block)
    {
        XOR_ASSERT(block.begin >= 0 && block.begin < m_size,
                   "Released block is out of bounds");
        XOR_ASSERT(block.end > 0 && block.end <= m_size,
                   "Released block is out of bounds");
        XOR_ASSERT(block.size() > 0, "Released block is empty");

        // Check if there's a block on the left we can merge with.
        auto left = m_blocksToCoalesce.find(block.begin);
        if (left != m_blocksToCoalesce.end())
        {
            // Yes there is, merge it to the block being released.
            block.begin = left->second.begin;
            m_blocksToCoalesce.erase(left);
        }

        // Check on the right.
        auto right = m_blocksToCoalesce.find(block.end);
        if (right != m_blocksToCoalesce.end())
        {
            block.end = right->second.end;
            m_blocksToCoalesce.erase(right);
        }

        // Insert this block in the coalescing table.
        m_blocksToCoalesce[block.begin] = block;
        m_blocksToCoalesce[block.end]   = block;

        // Determine the size class to put this block in.
        size_t size = block.size();
        uint alignment = static_cast<uint>(firstbitlow(static_cast<uint64_t>(block.begin)));
        // Clamp it to the maximum representable alignment.
        alignment = std::min(alignment, 1U << (AlignmentBits - 1));
        auto key = encodeSizeAlignment(size, alignment);

        // Finally, insert it to the free list.
        m_sizeBins[key].freeOffsets.emplace(block.begin);
    }

    bool OffsetHeap::markAsAllocated(Block block)
    {
        return false;
    }
}
