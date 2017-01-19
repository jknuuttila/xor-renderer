#include "Xor/XorDeviceState.hpp"
#include "Xor/XorCommandList.hpp"

namespace xor
{
    constexpr uint MaxRTVs = 256;
    constexpr uint MaxDSVs = 256;
    constexpr uint DescriptorHeapSize = 65536 * 4;
    constexpr uint DescriptorHeapRing = 65536 * 3;
    constexpr uint QueryHeapSize = 65536;

    namespace backend
    {
        DeviceState::DeviceState(Adapter adapter_, ComPtr<ID3D12Device> pDevice, std::shared_ptr<backend::ShaderLoader> pShaderLoader)
        {

            adapter       = std::move(adapter_);
            device        = std::move(pDevice);
            shaderLoader  = std::move(pShaderLoader);

            {
                D3D12_COMMAND_QUEUE_DESC desc ={};
                desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
                desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
                desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
                desc.NodeMask = 0;
                XOR_CHECK_HR(device->CreateCommandQueue(
                    &desc,
                    __uuidof(ID3D12CommandQueue),
                    &graphicsQueue));
            }

            XOR_INTERNAL_DEBUG_NAME(graphicsQueue);

            uploadHeap   = std::make_shared<UploadHeap>(device.Get(), progress);
            readbackHeap = std::make_shared<ReadbackHeap>(device.Get(), progress);

            rtvs = ViewHeap(device.Get(),
                            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                            "rtvs",
                            MaxRTVs);
            dsvs = ViewHeap(device.Get(),
                            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                            "dsvs",
                            MaxDSVs);
            shaderViews = ViewHeap(device.Get(),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                   "shaderViews",
                                   DescriptorHeapSize, DescriptorHeapRing);

			queryHeap = std::make_shared<QueryHeap>(device.Get(), QueryHeapSize);

            nullTextureSRV = shaderViews.allocateFromHeap();
            nullTextureUAV = shaderViews.allocateFromHeap();

            {
                D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
                desc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
                desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                desc.Texture2D.MipLevels           = static_cast<UINT>(-1);
                desc.Texture2D.MostDetailedMip     = 0;
                desc.Texture2D.PlaneSlice          = 0;
                desc.Texture2D.ResourceMinLODClamp = 0;
                device->CreateShaderResourceView(
                    nullptr,
                    &desc,
                    nullTextureSRV.cpu);
            }

            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
                desc.Format                           = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice               = 0;
                desc.Texture2D.PlaneSlice             = 0;
                device->CreateUnorderedAccessView(
                    nullptr, nullptr,
                    &desc,
                    nullTextureUAV.cpu);
            }
        }

        DeviceState::~DeviceState()
        {
            progress.waitUntilDrained();
#if 0
            ComPtr<ID3D12DebugDevice> debug;
            XOR_CHECK_HR(device.As(&debug));
            debug->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
#endif
        }

        void GPUProgressTracking::executeCommandList(CommandList && cmd)
        {
            newestExecuted = std::max(newestExecuted, cmd.number());
            executedCommandLists.emplace_back(std::move(cmd));
        }

        void GPUProgressTracking::retireCommandLists()
        {
            uint completedLists = 0;

            for (auto &cmd : executedCommandLists)
            {
                if (cmd.hasCompleted())
                    ++completedLists;
                else
                    break;
            }

            for (uint i = 0; i < completedLists; ++i)
            {
                auto &cmd = executedCommandLists[i];
                commandListSequence.complete(cmd.number());
            }

            // This will also return the command list states to the pool
            executedCommandLists.erase(executedCommandLists.begin(),
                                       executedCommandLists.begin() + completedLists);

            while (!completionCallbacks.empty())
            {
                auto &top = completionCallbacks.top();
                if (commandListSequence.hasCompleted(top.seqNum))
                {
                    top.f();
                    completionCallbacks.pop();
                }
                else
                {
                    break;
                }
            }
        }

        bool GPUProgressTracking::hasBeenExecuted(SeqNum seqNum)
        {
            if (hasCompleted(seqNum))
                return true;

            if (seqNum > newestExecuted)
                return false;

            for (auto &c : executedCommandLists)
            {
                if (c.number() == seqNum)
                    return true;
            }

            return false;
        }

        void GPUProgressTracking::waitUntilCompleted(SeqNum seqNum)
        {
            while (!hasCompleted(seqNum))
            {
                auto &executed = executedCommandLists;
                XOR_CHECK(!executed.empty(), "Nothing to wait for, deadlock!");
                executed.front().waitUntilCompleted();
            }
        }

		QueryHeap::QueryHeap(ID3D12Device * device, size_t size)
		{
			size_t numTimestamps = 2 * size;

			D3D12_HEAP_PROPERTIES heapDesc ={};
			heapDesc.Type                 = D3D12_HEAP_TYPE_READBACK;
			heapDesc.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heapDesc.CreationNodeMask     = 0;
			heapDesc.VisibleNodeMask      = 0;

			D3D12_RESOURCE_DESC desc ={};
			desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			desc.Width              = numTimestamps * sizeof(uint64_t);
			desc.Height             = 1;
			desc.DepthOrArraySize   = 1;
			desc.MipLevels          = 1;
			desc.Format             = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count   = 1;
			desc.SampleDesc.Quality = 0;
			desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

			XOR_CHECK_HR(device->CreateCommittedResource(
				&heapDesc,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				__uuidof(ID3D12Resource),
				&readback));
			setName(readback, "QueryHeap readback");

			D3D12_QUERY_HEAP_DESC timestampDesc = {};
			timestampDesc.Type                  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			timestampDesc.Count                 = static_cast<UINT>(size * 2);
			timestampDesc.NodeMask              = 0;

			XOR_CHECK_HR(device->CreateQueryHeap(
				&timestampDesc,
				__uuidof(ID3D12QueryHeap),
				&timestamps));

			ringbuffer = OffsetRing(size);
			metadata.resize(size);
		}

		void QueryHeap::resolve(ID3D12GraphicsCommandList * cmdList, int64_t first, int64_t last)
        {
            if (last >= first)
            {
                uint start = static_cast<uint>(first * 2);
                uint count = static_cast<uint>((last - first + 1) * 2);

                cmdList->ResolveQueryData(
                    timestamps.Get(),
                    D3D12_QUERY_TYPE_TIMESTAMP,
                    start, count,
                    readback.Get(),
                    start * sizeof(uint64_t));
            }
            else
            {
                // If the ring buffer wrapped around, we need to copy both halves

                uint size = static_cast<uint>(ringbuffer.size());

                uint startHigh = static_cast<uint>(first * 2);
                uint countHigh = static_cast<uint>((size - first) * 2);
                uint startLow  = 0;
                uint countLow  = static_cast<uint>(last * 2);

                cmdList->ResolveQueryData(
                    timestamps.Get(),
                    D3D12_QUERY_TYPE_TIMESTAMP,
                    startHigh, countHigh,
                    readback.Get(),
                    startHigh * sizeof(uint64_t));
                cmdList->ResolveQueryData(
                    timestamps.Get(),
                    D3D12_QUERY_TYPE_TIMESTAMP,
                    startLow, countLow,
                    readback.Get(),
                    startLow * sizeof(uint64_t));
            }
		}

		int64_t QueryHeap::beginEvent(ID3D12GraphicsCommandList * cmdList,
                                      const char * name, bool print,
                                      SeqNum cmdListNumber)
		{
			int64_t offset = ringbuffer.allocate();
			XOR_CHECK(offset >= 0, "Out of ringbuffer space");
			auto &m         = metadata[offset];
			m.name          = name;
			m.cmdListNumber = cmdListNumber;
			m.parent        = top;
            m.print         = print;

			cmdList->EndQuery(timestamps.Get(),
							  D3D12_QUERY_TYPE_TIMESTAMP,
							  static_cast<UINT>(offset * 2));

			top = offset;

			return offset;
		}

		void QueryHeap::endEvent(ID3D12GraphicsCommandList * cmdList, int64_t eventOffset)
		{
			XOR_CHECK(eventOffset >= 0, "Invalid event");
			auto &m = metadata[eventOffset];

			cmdList->EndQuery(timestamps.Get(),
							  D3D12_QUERY_TYPE_TIMESTAMP,
							  static_cast<UINT>(eventOffset * 2 + 1));

			top = m.parent;
		}

#define XOR_GPU_TRANSIENT_VERBOSE_LOGGING

#if defined(XOR_GPU_TRANSIENT_VERBOSE_LOGGING)
#define XOR_GPU_TRANSIENT_VERBOSE(fmt, ...) log("GPUTransientMemoryAllocator", "\"%s\": " fmt, m_name.cStr(), ## __VA_ARGS__)
#else
#define XOR_GPU_TRANSIENT_VERBOSE(...)
#endif
#define XOR_GPU_TRANSIENT_BLOCK(block_) lld((block_).block.begin), lld((block_).block.end), (block_).block.size(), lld((block_).allocatedBy)
#define XOR_GPU_TRANSIENT_FREEBLOCK(block_) lld((block_).freeBlock.begin), lld((block_).freeBlock.end), (block_).freeBlock.size(), lld((block_).allocatedBy)

        int GPUTransientMemoryAllocator::addBlockAfter(AllocationBlock newBlock,
                                                       AllocationBlock & predecessor)
        {
            newBlock.prev = indexOfBlock(predecessor);
            newBlock.next = predecessor.next;

            int newBlockIndex = static_cast<int>(m_blocks.size());
            predecessor.next = newBlockIndex;

            m_blocks.emplace_back(newBlock);
            return newBlockIndex;
        }

        void GPUTransientMemoryAllocator::removeBlock(AllocationBlock & b)
        {
            XOR_ASSERT(m_blocks.size() > 1, "There must be at least one block in the list");

            int blockIndex = indexOfBlock(b);
            int lastIndex = static_cast<int>(m_blocks.size() - 1);

            // First, put the to-be-removed block as the last block
            // in the list, fixing any pointers to the previously-last block.
            auto &last = m_blocks[lastIndex];
            if (blockIndex != lastIndex)
            {
                prev(last).next = blockIndex;
                next(last).prev = blockIndex;
                if (m_current == lastIndex) m_current = blockIndex;
                std::swap(b, last);
            }

            // Now, the block is the last block. Remove it from the chain.
            prev(last).next = last.next;
            next(last).prev = last.prev;
            if (m_current == lastIndex) m_current = last.next;

            // Finally, pop it off the list.
            m_blocks.pop_back();
        }

        inline Block GPUTransientMemoryAllocator::allocate(GPUProgressTracking & progress,
                                                           size_t size, size_t alignment,
                                                           SeqNum cmdList)
        {
            XOR_GPU_TRANSIENT_VERBOSE("Allocating %zu into command list %lld. Current free block is (%lld, %lld, %zu, %lld).\n",
                                      size, lld(cmdList),
                                      XOR_GPU_TRANSIENT_FREEBLOCK(current()));
            return allocate(progress, current(), size, alignment, cmdList);
        }

        Block GPUTransientMemoryAllocator::allocate(GPUProgressTracking & progress,
                                                    AllocationBlock & allocateFrom,
                                                    size_t size, size_t alignment,
                                                    SeqNum cmdList)
        {
            // Check if we are eligible to allocate from it.
            if (blockIsFree(progress, allocateFrom) || allocateFrom.allocatedBy == cmdList)
            {
                // We are. Check if the block can hold the alloc.
                int64_t begin = allocateFrom.freeBlock.fitAtBegin(size, alignment);
                if (begin >= 0)
                {
                    // Yes, it can. Adjust the free block and we're done.
                    int64_t end = begin + static_cast<int64_t>(size);
                    allocateFrom.allocatedBy = cmdList;
                    allocateFrom.freeBlock.begin = end;
                    m_current = indexOfBlock(allocateFrom);
                    Block b(begin, end);
                    XOR_ASSERT(begin >= 0 && end <= m_size, "halp");
                    XOR_GPU_TRANSIENT_VERBOSE("    Allocated (%lld, %lld). Free block is now (%lld, %lld, %zu, %lld).\n",
                                              b.begin, b.end,
                                              XOR_GPU_TRANSIENT_FREEBLOCK(allocateFrom));
                    return b;
                }
            }

            // If we got here, the current block is either too small or
            // belongs to another command list, and we can't use it.

            // If it belongs to another command list, split its free space into
            // a free block and retry.
            if (!allocateFrom.isFree())
            {
                XOR_GPU_TRANSIENT_VERBOSE("    Current block belongs to another command list or is too small. Splitting leftovers.\n");
                int freeBlock = splitFreeBlock(allocateFrom);
                // This recursive call is guaranteed to enter the first branch,
                // avoiding infinite recursion.
                return allocate(progress, m_blocks[freeBlock],
                                size, alignment, cmdList);
            }

            // If we got here, the current block is free, but too small. 
            // Attempt to make it bigger by merging subsequent blocks with it.
            // If that makes the block bigger, retry.
            XOR_GPU_TRANSIENT_VERBOSE("    Current block is too small. Attempting to merge.\n");
            if (mergeSubsequentFreeBlocks(progress, allocateFrom))
                return allocate(progress, size, alignment, cmdList);

            // If that fails, try to find another free block in the list to allocate from.
            XOR_GPU_TRANSIENT_VERBOSE("    Merging failed. Attempting to find another free block.\n");
            auto next = nextFreeBlock(progress, allocateFrom);

            // If it doesn't exist or it's the current active block, the allocation
            // is impossible, and we must wait for more space to free up. Otherwise,
            // retry the allocation from there.
            if (next && next != &current())
                return allocate(progress, *next, size, alignment, cmdList);

            XOR_GPU_TRANSIENT_VERBOSE("    No free blocks found, waiting for one to release.\n");
            auto freed = waitForFreeBlocks(progress);
            XOR_CHECK(!!freed, "Cannot satisfy allocation and there are no command lists to wait for. Deadlock.");

            // Try to merge additional free blocks from both sides.

            // If there is a contigous free block on the left, it will merge both
            // this block and any subsequent free blocks with itself.
            auto &prevBlock = prev(*freed);
            if (blocksAreContiguous(prevBlock, *freed) && blockIsFree(progress, prevBlock))
            {
                XOR_GPU_TRANSIENT_VERBOSE("    Merging newly released block with another on the left.\n");
                m_current = indexOfBlock(prevBlock);
                mergeSubsequentFreeBlocks(progress, current());
                return allocate(progress, size, alignment, cmdList);
            }
            // If not, try to merge subsequent blocks to this one.
            else
            {
                XOR_GPU_TRANSIENT_VERBOSE("    Merging newly released block with those on the right.\n");
                m_current = indexOfBlock(*freed);
                mergeSubsequentFreeBlocks(progress, current());
                return allocate(progress, size, alignment, cmdList);
            }
        }

        int GPUTransientMemoryAllocator::splitFreeBlock(AllocationBlock & b)
        {
            if (b.isFree())
                return indexOfBlock(b);

            // Take the free space in the block.
            AllocationBlock freeBlock(b.freeBlock);
            // Mark the block as having no free space left.
            b.block.end     = b.freeBlock.begin;
            b.freeBlock.end = b.freeBlock.begin;

            XOR_GPU_TRANSIENT_VERBOSE("    Split (%lld, %lld, %lld) into (%lld, %lld, %zu, %lld) and (%lld, %lld, %zu, %lld)\n",
                                      lld(b.block.begin), lld(freeBlock.block.end), b.allocatedBy,
                                      XOR_GPU_TRANSIENT_BLOCK(b),
                                      XOR_GPU_TRANSIENT_BLOCK(freeBlock));
            return addBlockAfter(freeBlock, b);
        }

        bool GPUTransientMemoryAllocator::mergeSubsequentFreeBlocks(GPUProgressTracking & progress,
                                                                    AllocationBlock & start)
        {
            bool merged = false;

            XOR_ASSERT(start.isFree(), "Block to merge is not free");

            AllocationBlock *b = &next(start);

            for (;;)
            {
                XOR_GPU_TRANSIENT_VERBOSE("        Considering merge block (%lld, %lld, %zu, %lld)\n",
                                          XOR_GPU_TRANSIENT_BLOCK(*b));

                // If we come full circle, we are done.
                if (b->block.begin == start.block.begin)
                    return merged;

                // If the blocks are not contiguous, they cannot be merged.
                // This happens when the allocator wraps around its space.
                if (!blocksAreContiguous(start, *b))
                    return merged;

                // If we encounter a block that is still in use, we are also done.
                if (!blockIsFree(progress, *b))
                    return merged;

                // If we get here, the block is free.
                XOR_GPU_TRANSIENT_VERBOSE("        Merging (%lld, %lld, %zu, %lld) and (%lld, %lld, %zu, %lld)\n",
                                          XOR_GPU_TRANSIENT_BLOCK(start),
                                          XOR_GPU_TRANSIENT_BLOCK(*b));

                // Merge it to our current block.
                start.block.end = b->block.end;
                start.freeBlock = start.block;

                // Then remove it from the list and proceed with the next block.
                b = &next(*b);
                removeBlock(*b);

                merged = true;
            }
        }

        GPUTransientMemoryAllocator::AllocationBlock * GPUTransientMemoryAllocator::nextFreeBlock(
            GPUProgressTracking & progress, AllocationBlock & b)
        {
            AllocationBlock *block = &next(b);
            for (;;)
            {
                XOR_GPU_TRANSIENT_VERBOSE("        Testing if block (%lld, %lld, %zu, %lld) is free\n",
                                          XOR_GPU_TRANSIENT_BLOCK(*block));
                if (block == &b)
                {
                    XOR_GPU_TRANSIENT_VERBOSE("        Reached starting block again, no blocks were free\n");
                    return nullptr;
                }
                else if (blockIsFree(progress, *block))
                {
                    XOR_GPU_TRANSIENT_VERBOSE("        Found next free block: (%lld, %lld, %zu, %lld)\n",
                                              XOR_GPU_TRANSIENT_BLOCK(*block));
                    return block;
                }
                else
                {
                    block = &next(*block);
                }
            }
        }

        GPUTransientMemoryAllocator::AllocationBlock * GPUTransientMemoryAllocator::waitForFreeBlocks(
            GPUProgressTracking & progress)
        {
            SeqNum oldestExecutedSeqNum;
            AllocationBlock *oldestBlockWithExecutedList = nullptr;

            for (auto &b : m_blocks)
            {
                // Skip all known-free blocks, since they have already been
                // tried if we get here.
                if (b.isFree())
                    continue;

                // If we find a block that just released, we don't need to
                // wait.
                if (progress.hasCompleted(b.allocatedBy))
                {
                    XOR_GPU_TRANSIENT_VERBOSE("        Found just released block (%lld, %lld, %zu, %lld)\n",
                                              XOR_GPU_TRANSIENT_BLOCK(b));
                    b.release();
                    return &b;
                }

                // Only consider waiting for executed lists, as obviously
                // otherwise we will deadlock. Testing can be expensive,
                // but we're going to wait anyway, so it doesn't matter.
                if (progress.hasBeenExecuted(b.allocatedBy))
                {
                    if (!oldestBlockWithExecutedList
                        || b.allocatedBy < oldestExecutedSeqNum)
                    {
                        oldestBlockWithExecutedList = &b;
                        oldestExecutedSeqNum = b.allocatedBy;
                    }
                }
            }

            if (oldestBlockWithExecutedList)
            {
                XOR_GPU_TRANSIENT_VERBOSE("        Waiting for block (%lld, %lld, %zu, %lld) to release\n",
                                          XOR_GPU_TRANSIENT_BLOCK(*oldestBlockWithExecutedList));
                progress.waitUntilCompleted(oldestBlockWithExecutedList->allocatedBy);
                oldestBlockWithExecutedList->release();
                return oldestBlockWithExecutedList;
            }
            else
            {
                return nullptr;
            }
        }

        bool GPUTransientMemoryAllocator::blockIsFree(GPUProgressTracking & progress,
                                                      AllocationBlock & b)
        {
            if (b.isFree())
            {
                return true;
            }
            else if (progress.hasCompleted(b.allocatedBy))
            {
                XOR_GPU_TRANSIENT_VERBOSE("        Block (%lld, %lld, %zu, %lld) is now free\n",
                                          XOR_GPU_TRANSIENT_BLOCK(b));
                b.release();
                return true;
            }
            else
            {
                return false;
            }
        }
}
}
