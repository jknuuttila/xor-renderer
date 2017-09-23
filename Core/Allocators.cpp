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
        SizeAlignment sa = size << AlignmentBits;
        XOR_ASSERT(popCount(alignment) == 1, "Alignment must be a power of 2");
        sa |= static_cast<uint64_t>(countTrailingZeros(alignment));
        return sa;
    }

    size_t OffsetHeap::decodeSize(SizeAlignment sa)
    {
        return sa >> AlignmentBits;
    }

    uint OffsetHeap::decodeAlignment(SizeAlignment sa)
    {
        static const uint64_t AlignmentMask = (1ULL << AlignmentBits) - 1;
        uint64_t log2Alignment = sa & AlignmentMask;
        return 1U << static_cast<uint>(log2Alignment);
    }

    bool OffsetHeap::canFit(SizeAlignment blockSA, size_t size, uint alignment)
    {
        auto freeSize      = decodeSize(blockSA);
        auto freeAlignment = decodeAlignment(blockSA);

        // If the block is too small, it cannot fit no matter what.
        if (freeSize < size)
            return false;

        // If the block is big enough, it always fits if it as least as much
        // aligned.
        if (freeAlignment >= alignment)
            return true;

        // If the block has smaller alignment than required, then extra space is necessary.
        // The required extra space is equal to the difference in alignments.
        auto requiredSize = size + (alignment - freeAlignment);
        return freeSize >= requiredSize;
    }

    void OffsetHeap::allocateBlock(Block block)
    {
        auto erasedBegin = m_blocksToCoalesce.erase(block.begin);
        auto erasedEnd   = m_blocksToCoalesce.erase(block.end);
        XOR_ASSERT(erasedBegin == 1, "The allocated block was not free");
        XOR_ASSERT(erasedEnd == 1, "The allocated block was not free");
        XOR_ASSERT(m_freeSpace >= block.size(),
                   "Allocating block that is larger than the free size");
        m_freeSpace -= block.size();
    }

    bool OffsetHeap::resize(size_t newSize)
    {
        int64_t iNewSize = static_cast<int64_t>(newSize);

        if (iNewSize == m_size)
            return true;

        if (iNewSize > m_size)
        {
            // We can grow the allocator just by releasing a block past the end.
            Block b(m_size, iNewSize);
            m_size = iNewSize;
            release(b);
            return true;
        }
        else
        {
            // Check if there is a free block that ends at the heap end.
            auto it = m_blocksToCoalesce.find(m_size);
            if (it == m_blocksToCoalesce.end())
            {
                // There is not, so the heap end is allocated and we
                // cannot shrink.
                return false;
            }

            Block freeBlock(it->second, m_size);
            if (iNewSize >= freeBlock.begin)
            {
                m_size = iNewSize;
                // Cut off the reduced part of the final free block.
                allocateBlock(freeBlock);
                if (freeBlock.begin < iNewSize)
                    release(Block(freeBlock.begin, iNewSize));

                return true;
            }
            else
            {
                // The free space at the heap end is not big enough.
                return false;
            }
        }
    }

    Block OffsetHeap::allocate(size_t size)
    {
        return allocate(size, m_minAlignment);
    }

    Block OffsetHeap::allocate(size_t size, uint alignment)
    {
        alignment = std::max(alignment, m_minAlignment);
        size = roundUpToMultiple<size_t>(size, alignment);

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

                // Find the actual offset for it.
                auto &sizeBin = it->second;
                XOR_ASSERT(!sizeBin.freeOffsets.empty(),
                           "Size bins should always be non-empty.");
                int64_t offset = sizeBin.freeOffsets.top();
                sizeBin.freeOffsets.pop();

                Block entireBlock;
                entireBlock.begin = offset;
                entireBlock.end   = offset
                                  + static_cast<int64_t>(decodeSize(it->first));

                // If we just emptied the size bin, remove it from the
                // map.
                if (sizeBin.freeOffsets.empty())
                    m_sizeBins.erase(it);

                // Mark the space as allocated. We might chop off and re-release
                // some parts after.
                allocateBlock(entireBlock);

                // Check if we need to chop off extra bits.
                if (entireBlock.size() == size)
                {
                    // The block is an exact match, so no leftovers.
                    return entireBlock;
                }
                else
                {
                    // First, find the first properly aligned offset
                    // from within the block.
                    Block b;
                    b.begin = roundUpToMultiple<int64_t>(entireBlock.begin, alignment);
                    b.end   = b.begin + size;

                    XOR_ASSERT(b.end <= entireBlock.end,
                               "Allocated block does not fit in the free block");

                    // Was there space left in the beginning because
                    // of alignment?
                    if (b.begin > entireBlock.begin)
                    {
                        Block prefix(entireBlock.begin, b.begin);
                        release(prefix);
                    }

                    // Was there space left in the end because the
                    // block was larger than needed?
                    if (b.end < entireBlock.end)
                    {
                        Block suffix(b.end, entireBlock.end);
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
            XOR_ASSERT(left->second < block.begin,
                       "Coalesced block invalid");
            block.begin = left->second;
            m_blocksToCoalesce.erase(left);
        }

        // Check on the right.
        auto right = m_blocksToCoalesce.find(block.end);
        if (right != m_blocksToCoalesce.end())
        {
            XOR_ASSERT(right->second > block.end,
                       "Coalesced block invalid");
            block.end = right->second;
            m_blocksToCoalesce.erase(right);
        }

        // Insert this block in the coalescing table.
        m_blocksToCoalesce[block.begin] = block.end;
        m_blocksToCoalesce[block.end]   = block.begin;

        // Determine the size class to put this block in.
        size_t size = block.size();
        XOR_ASSERT(size % m_minAlignment == 0,
                   "All blocks must be aligned by the minimum alignment");
        uint alignment = 1U << countTrailingZeros(static_cast<uint64_t>(block.begin));
        auto key = encodeSizeAlignment(size, alignment);

        // Finally, insert it to the free list.
        m_sizeBins[key].freeOffsets.emplace(block.begin);

        m_freeSpace += block.size();
        XOR_ASSERT(m_freeSpace <= static_cast<size_t>(m_size),
                   "More free space than the total size");
    }

    bool OffsetHeap::markAsAllocated(Block block)
    {
        XOR_ASSERT(block.begin % m_minAlignment == 0,
                   "Allocated blocks must be aligned by the minimum alignment");
        XOR_ASSERT(block.end % m_minAlignment == 0,
                   "Allocated blocks must be aligned by the minimum alignment");
        // All free blocks can be found from the m_blocksToCoalesce structure.
        // Furthermore, it is guaranteed that there are allocated gaps
        // between them, because otherwise they would have been coalesced.
        // This means that our block must fit entirely within one free block,
        // or we cannot mark it as allocated.

        for (auto &&kv : m_blocksToCoalesce)
        {
            Block freeBlock(std::min(kv.first, kv.second),
                            std::max(kv.first, kv.second));

            if (freeBlock.begin <= block.begin &&
                freeBlock.end   >= block.end)
            {
                // We found the free block that contains our block.
                // Mark it as allocated and then re-release the parts
                // before and after our block.
                allocateBlock(freeBlock);

                if (freeBlock.begin < block.begin)
                    release(Block(freeBlock.begin, block.begin));

                if (freeBlock.end > block.end)
                    release(Block(block.end, freeBlock.end));

                return true;
            }
        }

        return false;
    }

    Block Block::fitAtBegin(size_t size, size_t alignment) const
    {
        int64_t iSize = static_cast<int64_t>(size);
        int64_t alignedBegin = roundUpToMultiple(begin, static_cast<int64_t>(alignment));
        if (alignedBegin + iSize > end)
            return Block();
        else
            return Block(alignedBegin, alignedBegin + iSize);
    }

    Block32 Block32::fitAtBegin(size_t size, size_t alignment) const
    {
        int32_t iSize = static_cast<int32_t>(size);
        int32_t alignedBegin = roundUpToMultiple(begin, static_cast<int32_t>(alignment));
        if (alignedBegin + iSize > end)
            return Block32();
        else
            return Block32(alignedBegin, alignedBegin + iSize);
    }
}
