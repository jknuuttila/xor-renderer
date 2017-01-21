#pragma once

#include "Utils.hpp"
#include "Error.hpp"

#include <vector>
#include <map>
#include <unordered_map>
#include <queue>
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

    struct Block
    {
        int64_t begin = -1;
        int64_t end   = -1;

        Block() = default;
        Block(int64_t begin, int64_t end)
            : begin(begin)
            , end(end)
        {}

        bool valid() const { return begin >= 0; }
        explicit operator bool() const { return valid(); }

        bool empty() const { return size() == 0; }
        size_t size() const { return static_cast<size_t>(end - begin); }

        Block fitAtBegin(size_t size, size_t alignment = 1) const;

        bool canFit(size_t size, size_t alignment = 1) const
        {
            return !!fitAtBegin(size, alignment);
        }
    };

    // Allocating and releasing in a ring buffer fashion (i.e. FIFO),
    // with support for contiguous longer allocations and alignments.
    class OffsetRing
    {
        // The oldest allocated element, unless equal to tail.
        int64_t m_head = 0;
        // The first free element.
        int64_t m_tail = 0;
        // Amount of space in the ring.
        int64_t m_size = 0;
        // Always false unless head == tail. If head == tail,
        // the ring is completely empty if m_full == false,
        // and completely full if m_full == true.
        bool    m_full = false;
    public:
        OffsetRing() = default;
        OffsetRing(size_t size) : m_size(static_cast<int64_t>(size)) {}

        void clear()
        {
            m_head = 0;
            m_tail = 0;
            m_full = false;
        }

        bool empty() const { return m_head == m_tail && !m_full; }
        bool full() const  { return m_full; }
        size_t size() const { return m_size; }
        size_t freeSpace() const;

        int64_t oldest() const { return empty() ? -1 : m_head; }
        int64_t newest() const
        {
            if (empty())
                return -1;

            int64_t newest = m_tail - 1;

            if (newest < 0)
                newest += m_size;

            return newest;
        }

        int64_t allocate();
        int64_t allocateContiguous(size_t amount);
        int64_t allocateContiguous(size_t amount, size_t alignment);
        void releaseEnd(int64_t onePastLastOffset);

        void releaseUntil(int64_t lastOffset)
        {
            int64_t end = lastOffset + 1;
            if (end == m_size)
                end = 0;
            releaseEnd(end);
        }

        void release(int64_t offset, size_t amount = 1)
        {
            int64_t end = offset + static_cast<int64_t>(amount);
            if (end >= m_size)
                end -= m_size;
            releaseEnd(end);
        }

        Block allocateBlock(size_t amount)
        {
            Block block;
            block.begin = allocateContiguous(amount);
            block.end   = block.begin + static_cast<int64_t>(amount);
            return block;
        }

        Block allocateBlock(size_t amount, size_t alignment)
        {
            Block block;
            block.begin = allocateContiguous(amount, alignment);
            block.end   = block.begin + static_cast<int64_t>(amount);
            return block;
        }

        void release(Block block)
        {
            release(block.begin, static_cast<size_t>(block.end - block.begin));
        }
    };

    // Generic best-fit address-ordered heap suballocator.
    class OffsetHeap
    {
        // Size and alignment are bit-packed so that alignment is in
        // the low-order bits. This way, bigger sizes have larger numbers,
        // but bigger alignments are larger than smaller alignments
        // in the same size class.
        static const uint AlignmentBits = 6;
        using SizeAlignment = uint64_t;

        struct SizeBin
        {
            // Every free offset of this size bin in a priority
            // queue so we can always obtain the lowest address.
            std::priority_queue<int64_t> freeOffsets;
        };

        // Contains the address-ordered free list of each non-empty
        // size class. Empty size bins are removed from the map.
        std::map<SizeAlignment, SizeBin> m_sizeBins;
        // Each released block is stored here with both its begin
        // and end offsets as keys. The begin key will have the end
        // as the value and vice versa. Whenever a new block is released,
        // it can check for its own begin and end here to coalesce
        // with its neighbor blocks to form larger ones.
        std::unordered_map<int64_t, int64_t> m_blocksToCoalesce;

        // Total size of the heap managed by this allocator.
        int64_t m_size      = 0;
        uint m_minAlignment = 1;
        size_t m_freeSpace  = 0;
    public:
        OffsetHeap() = default;
        OffsetHeap(size_t size, uint minimumAlignment = 1);

        bool empty() const { return freeSpace() == static_cast<size_t>(m_size); }
        bool full() const { return freeSpace() == 0; }
        size_t freeSpace() const { return m_freeSpace; }

        // Attempt to shrink or grow this allocator. Growing always succeeds,
        // but shrinking fails if it would turn allocated areas invalid.
        // Returns true on success.
        bool resize(size_t newSize);

        // Allocate a block of the given size using the minimum alignment
        // of this allocator. The size is rounded up to an aligned multiple.
        // Return an invalid block on failure.
        Block allocate(size_t size);
        // Allocate a block with the given size and alignment.
        // The size is rounded up to be aligned with the minimum alignment.
        // Return an invalid block on failure.
        Block allocate(size_t size, uint alignment);
        // Release a previously allocated block.
        void release(Block block);
        // Try to mark the given block (which should currently be free)
        // as allocated. Return true on success, and false if some part
        // of the block was allocated already.
        bool markAsAllocated(Block block);

    private:
        SizeAlignment encodeSizeAlignment(size_t size, uint alignment);
        size_t decodeSize(SizeAlignment sa);
        uint decodeAlignment(SizeAlignment sa);
        bool canFit(SizeAlignment blockSA, size_t size, uint alignment);
        void allocateBlock(Block block);
    };
}

