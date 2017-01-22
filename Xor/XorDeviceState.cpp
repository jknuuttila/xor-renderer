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

// #define XOR_GPU_TRANSIENT_VERBOSE_LOGGING

#if defined(XOR_GPU_TRANSIENT_VERBOSE_LOGGING)
#define XOR_GPU_TRANSIENT_VERBOSE(fmt, ...) log("GPUTransientMemoryAllocator", "\"%s\": " fmt, m_name.cStr(), ## __VA_ARGS__)
#else
#define XOR_GPU_TRANSIENT_VERBOSE(...)
#endif

        GPUTransientMemoryAllocator::GPUTransientMemoryAllocator(size_t size, size_t chunkSize, String name)
            : m_size(static_cast<int64_t>(size))
            , m_chunkSize(static_cast<int64_t>(chunkSize))
            , m_freeChunks(size / chunkSize)
            , m_name(std::move(name))
        {
            m_usedChunks.reserve(m_freeChunks.size());

            for (size_t i = 0; i < m_freeChunks.size(); ++i)
                m_freeChunks[i] = static_cast<int64_t>(i);
        }

        Block GPUTransientMemoryAllocator::allocate(GPUProgressTracking & progress, GPUTransientChunk & chunk,
                                                    size_t size, size_t alignment,
                                                    SeqNum cmdList)
        {
            size = roundUpToMultiple(size, alignment);

            auto &free = chunk.m_free;
            auto b = free.fitAtBegin(size, alignment);
            XOR_GPU_TRANSIENT_VERBOSE("Trying to allocate %zu for list %lld in existing chunk (%lld, %lld).\n",
                                      size, cmdList,
                                      free.begin, free.end);

            // If the allocation fits in the previous active chunk, just use that.
            if (b)
            {
                free.begin = b.end;
                XOR_GPU_TRANSIENT_VERBOSE("    Allocation successful. Chunk is now (%lld, %lld).\n",
                                          free.begin, free.end);
                return b;
            }
            // If not, get a new chunk.
            else
            {
                XOR_GPU_TRANSIENT_VERBOSE("    Existing chunk cannot hold allocation, getting new chunk.\n");

                XOR_CHECK(size <= static_cast<size_t>(m_chunkSize),
                          "Allocation does not fit in one chunk");

                ChunkNumber newChunk = findFreeChunk(progress);
                XOR_CHECK(newChunk >= 0, "There are no free or waitable chunks.");

                m_usedChunks.emplace_back(cmdList, newChunk);

                int64_t begin = newChunk * m_chunkSize;
                free = Block(begin, begin + m_chunkSize);

                auto b = free.fitAtBegin(size, alignment);
                XOR_ASSERT(b.valid(), "Allocation failed with an empty chunk");
                free.begin = b.end;
                return b;
            }
        }

        GPUTransientMemoryAllocator::ChunkNumber GPUTransientMemoryAllocator::findFreeChunk(GPUProgressTracking &progress)
        {
            // If there is a free chunk, we can just use it.
            if (!m_freeChunks.empty())
            {
                ChunkNumber c = m_freeChunks.back();
                m_freeChunks.pop_back();
                XOR_GPU_TRANSIENT_VERBOSE("Using free chunk %lld.\n", c);
                return c;
            }

            XOR_GPU_TRANSIENT_VERBOSE("No free chunks, checking for released chunks.\n");

            // If there is not, try to reclaim all chunks that have been released already.
            for (auto &c : m_usedChunks)
            {
                if (progress.hasCompleted(c.first))
                {
                    XOR_GPU_TRANSIENT_VERBOSE("    Chunk %lld, belonging to list %lld, was released, freeing.\n", c.second, c.first);
                    m_freeChunks.emplace_back(c.second);
                    c.first = -1;
                }
            }

            // Did we manage to find any? If so, get one and use it, and erase the
            // remnants from the used chunk list.
            if (!m_freeChunks.empty())
            {
                m_usedChunks.erase(
                    std::remove_if(m_usedChunks.begin(), m_usedChunks.end(),
                                   [] (auto &c) { return c.first < 0; }),
                    m_usedChunks.end());
                ChunkNumber c = m_freeChunks.back();
                m_freeChunks.pop_back();
                return c;
            }

            // No free chunks in the list. Wait for the first (presumably the oldest)
            // chunk that belongs to a list that has been executed.
            XOR_GPU_TRANSIENT_VERBOSE("No released chunks, waiting for a chunk.\n");
            for (size_t i = 0; i < m_usedChunks.size(); ++i)
            {
                auto &c = m_usedChunks[i];

                if (progress.hasBeenExecuted(c.first))
                {
                    XOR_GPU_TRANSIENT_VERBOSE("    Waiting for chunk %lld, belonging to list %lld.\n", c.second, c.first);
                    progress.waitUntilCompleted(c.first);
                    ChunkNumber newlyReleased = c.second;
                    m_usedChunks.erase(m_usedChunks.begin() + i);
                    return newlyReleased;
                }
            }

            // If we got here, there were no used chunks belonging to lists that
            // have been executed. Fail.
            return -1;
        }
    }
}
