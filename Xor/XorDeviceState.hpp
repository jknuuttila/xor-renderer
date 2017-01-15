#pragma once

#include "Xor/XorBackend.hpp"
#include "Xor/XorDevice.hpp"

namespace xor
{
    namespace backend
    {
        struct GPUProgressTracking
        {
            SequenceTracker commandListSequence;
            std::vector<CommandList> executedCommandLists;
            SeqNum newestExecuted = 0;
            std::priority_queue<CompletionCallback> completionCallbacks;

            SeqNum startNewCommandList()
            {
                return commandListSequence.start();
            }

            void executeCommandList(CommandList &&cmd);

            void retireCommandLists();

            SeqNum now()
            {
                return commandListSequence.newestStarted();
            }

            void whenCompleted(std::function<void()> f)
            {
                whenCompleted(std::move(f), now());
            }

            void whenCompleted(std::function<void()> f, SeqNum seqNum)
            {
                if (hasCompleted(seqNum))
                    f();
                else
                    completionCallbacks.emplace(seqNum, std::move(f));
            }

            bool hasCompleted(SeqNum seqNum)
            {
                retireCommandLists();
                return commandListSequence.hasCompleted(seqNum);
            }

            void waitUntilCompleted(SeqNum seqNum);

            void waitUntilDrained()
            {
                for (;;)
                {
                    auto newest = commandListSequence.newestStarted();
                    if (hasCompleted(newest))
                        break;
                    else
                        waitUntilCompleted(newest);
                }

                // When using WARP, the debug layer often complains
                // about releasing stuff too early, even if all command lists
                // executed by us have finished. Waiting a while seems to
                // work around this issue.
                Sleep(50);
            }
        };

        class GPUMemoryRingbuffer
        {
        private:
            OffsetRing            m_memoryRing;
            OffsetRing            m_metadataRing;

            struct Metadata
            {
                Block block;
                SeqNum allocatedBy;
            };
            std::vector<Metadata> m_metadata;
        public:
            GPUMemoryRingbuffer() = default;
            GPUMemoryRingbuffer(size_t memory,
                                size_t metadataEntries)
                : m_memoryRing(memory)
                , m_metadataRing(metadataEntries)
                , m_metadata(metadataEntries)
            {}

            Block allocate(size_t amount, size_t alignment, SeqNum cmdList)
            {
                if (m_metadataRing.full())
                    return Block();

                Metadata m;
                m.block = m_memoryRing.allocateBlock(amount, alignment);

                if (m.block.begin < 0)
                    return Block();

                m.allocatedBy = cmdList;

                auto metadataOffset = m_metadataRing.allocate();

                m_metadata[metadataOffset] = m;

                return m.block;
            }

            Block allocate(GPUProgressTracking &progress,
                           size_t amount, size_t alignment, SeqNum cmdList)
            {
                Block block = allocate(amount, alignment, cmdList);

                while (!block)
                {
                    auto oldest = oldestCmdList();

                    XOR_CHECK(oldest >= 0, "Ringbuffer not big enough to hold %llu elements.",
                              static_cast<llu>(amount));

                    progress.waitUntilCompleted(oldest);
                    while (oldest >= 0 && progress.hasCompleted(oldest))
                    {
                        releaseOldestAllocation();
                        oldest = oldestCmdList();
                    }

                    block = allocate(amount, alignment, cmdList);
                }


                return block;
            }

            SeqNum oldestCmdList() const
            {
                if (m_metadataRing.empty())
                    return -1;
                else
                    return m_metadata[m_metadataRing.oldest()].allocatedBy;
            }

            void releaseOldestAllocation()
            {
                XOR_ASSERT(!m_metadataRing.empty(), "Tried to release when ringbuffer empty.");
                XOR_ASSERT(!m_memoryRing.empty(), "Tried to release when ringbuffer empty.");
                auto allocOffset = m_metadataRing.oldest();
                auto &alloc = m_metadata[allocOffset];
                m_memoryRing.release(alloc.block);
                m_metadataRing.release(allocOffset);
            }
        };

        class ViewHeap
        {
            ComPtr<ID3D12DescriptorHeap> m_stagingHeap;
            ComPtr<ID3D12DescriptorHeap> m_heap;
            OffsetPool                   m_freeDescriptors;
            GPUMemoryRingbuffer          m_ring;
            uint                         m_ringStart = 0;
            D3D12_DESCRIPTOR_HEAP_TYPE   m_type      = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

            // We have two heaps and three types of descriptors:
            // - Actual descriptor heap used by shaders
            //    - CPU descriptors into it for some functions that write directly into it
            //    - GPU descriptors into it for some other functions
            //    - CPU cannot read this heap, only write
            // - Staging heap to be used as a copy source
            //    - Root argument setup copies view descriptors from there to the other heap
            //
            // - SRVs and UAVs are created in the staging heap, because they're only ever used
            //   by copying them.
            // - RTVs and DSVs are created in the shader heap, because they're never copied,
            //   only used directly.
            D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuStart  = { 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuStart  = { 0 };
            D3D12_CPU_DESCRIPTOR_HANDLE  m_stagingStart = { 0 };

            uint                         m_increment = 0;

            static const size_t ViewMetadataEntries = 4096;
        public:

            ViewHeap() = default;
            ViewHeap(ID3D12Device *device,
                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                     const String &name,
                     uint totalSize,
                     uint ringSize = 0)
            {
                m_type = type;

                D3D12_DESCRIPTOR_HEAP_DESC desc = {};
                desc.Type           = type;
                desc.NumDescriptors = totalSize;
                desc.Flags          = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                    ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                    : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                desc.NodeMask       = 0;

                XOR_CHECK_HR(device->CreateDescriptorHeap(
                    &desc,
                    __uuidof(ID3D12DescriptorHeap),
                    &m_heap));
                setName(m_heap, name);

                if (desc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
                {
                    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                    XOR_CHECK_HR(device->CreateDescriptorHeap(
                        &desc,
                        __uuidof(ID3D12DescriptorHeap),
                        &m_stagingHeap));
                    setName(m_stagingHeap, name + " staging");
                }

                m_ringStart       = totalSize - ringSize;
                m_freeDescriptors = OffsetPool(m_ringStart);
                m_ring            = GPUMemoryRingbuffer(ringSize,
                                                        ringSize ? ViewMetadataEntries : 0);
                m_increment       = device->GetDescriptorHandleIncrementSize(m_type);
                m_cpuStart        = m_heap->GetCPUDescriptorHandleForHeapStart();
                m_gpuStart        = m_heap->GetGPUDescriptorHandleForHeapStart();
                if (m_stagingHeap)
                    m_stagingStart = m_stagingHeap->GetCPUDescriptorHandleForHeapStart();
            }

            ID3D12DescriptorHeap *get() { return m_heap.Get(); }

            Descriptor descriptorAtOffset(int64_t offset)
            {
                Descriptor descriptor;
                descriptor.offset = offset;

                offset *= m_increment;

                descriptor.staging = m_stagingStart;
                descriptor.cpu     = m_cpuStart;
                descriptor.gpu     = m_gpuStart;

                descriptor.staging.ptr += offset;
                descriptor.cpu.ptr     += offset;
                descriptor.gpu.ptr     += offset;
                descriptor.type         = m_type;

                return descriptor;
            }

            Descriptor allocateFromHeap()
            {
                auto offset = m_freeDescriptors.allocate();
                XOR_CHECK(offset >= 0, "Ran out of descriptors in the heap.");
                return descriptorAtOffset(offset);
            }

            int64_t allocateFromRing(GPUProgressTracking &progress, size_t amount, SeqNum cmdList)
            {
                return m_ring.allocate(progress, amount, 1, cmdList).begin + m_ringStart;
            }

            void release(Descriptor descriptor)
            {
                XOR_ASSERT(descriptor.type == m_type, "Released descriptor to the wrong heap.");
                size_t offset = descriptor.cpu.ptr - m_cpuStart.ptr;
                offset /= m_increment;
                XOR_ASSERT(offset < m_ringStart, "Released descriptor out of bounds.");
                m_freeDescriptors.release(static_cast<int64_t>(offset));
            }
        };

        struct UploadHeap
        {
            static const size_t UploadHeapSize = 32 * 1024 * 1024;
            static const size_t UploadMetadataEntries = 4096;

            ComPtr<ID3D12Resource> heap;
            GPUMemoryRingbuffer ringbuffer;
            uint8_t *mapped;

            UploadHeap(ID3D12Device *device)
                : ringbuffer(UploadHeapSize, UploadMetadataEntries)
            {
                D3D12_HEAP_PROPERTIES heapDesc = {};
                heapDesc.Type                 = D3D12_HEAP_TYPE_UPLOAD;
                heapDesc.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                heapDesc.CreationNodeMask     = 0;
                heapDesc.VisibleNodeMask      = 0;

                D3D12_RESOURCE_DESC desc = {};
                desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
                desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                desc.Width              = UploadHeapSize;
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
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    __uuidof(ID3D12Resource),
                    &heap));
                setName(heap, "uploadHeap");

                mapHeap();
            }
            
            ~UploadHeap()
            {
                heap->Unmap(0, nullptr);
            }

            void mapHeap()
            {
                D3D12_RANGE dontRead;
                dontRead.Begin = 0;
                dontRead.End   = 0;
                void *p        = nullptr;
                XOR_CHECK_HR(heap->Map(0, &dontRead, &p));
                mapped = reinterpret_cast<uint8_t *>(p);
            }

            void flushBlock(Block block)
            {
                D3D12_RANGE flushRange;
                flushRange.Begin = static_cast<size_t>(block.begin);
                flushRange.End   = static_cast<size_t>(block.end);
                heap->Unmap(0, &flushRange);
                mapHeap();
            }

            Block uploadBytes(GPUProgressTracking &progress,
                              Span<const uint8_t> bytes,
                              SeqNum cmdListNumber,
                              uint alignment = 1)
            {
                auto block = ringbuffer.allocate(progress, bytes.sizeBytes(), alignment, cmdListNumber);
                memcpy(mapped + block.begin, bytes.data(), bytes.sizeBytes());
                flushBlock(block);
                return block;
            }
        };

		struct ProfilingEventData
		{
			const char *name;
			uint64_t ticks;
			int indent;
            bool print;
		};

        struct QueryHeap
        {
            ComPtr<ID3D12QueryHeap> timestamps;
            ComPtr<ID3D12Resource>  readback;
            struct Metadata
            {
                const char *name     = nullptr;
                int64_t parent       = -1;
                SeqNum cmdListNumber = -1;
                bool print           = false;
            };
            std::vector<Metadata> metadata;
            OffsetRing ringbuffer;
            int64_t top = -1;

			QueryHeap(ID3D12Device *device, size_t size);

			void resolve(ID3D12GraphicsCommandList *cmdList, int64_t first, int64_t last);

			int64_t beginEvent(ID3D12GraphicsCommandList *cmdList,
                               const char *name, bool print,
                               SeqNum cmdListNumber);
			void endEvent(ID3D12GraphicsCommandList *cmdList, int64_t eventOffset);

            template <typename F>
            void process(GPUProgressTracking &progress, F &&f)
            {
                int indent         = 0;
                int64_t prevParent = -1;

                int64_t i = ringbuffer.oldest();
                void *mappedReadback = nullptr;
                XOR_CHECK_HR(readback->Map(0, nullptr, &mappedReadback));
                auto queryData = reinterpret_cast<const uint64_t *>(mappedReadback);

                auto unmap = scopeGuard([&] {
                    D3D12_RANGE empty;
                    empty.Begin = 0;
                    empty.End   = 0;
                    readback->Unmap(0, &empty);
                });

                for (;;)
                {
                    if (i < 0)
                        break;

                    auto &m = metadata[i];

                    if (!progress.hasCompleted(m.cmdListNumber))
                        break;

                    if (m.parent > prevParent)
                        ++indent;
                    else if (m.parent < prevParent)
                        --indent;

					prevParent = m.parent;

                    uint64_t begin = queryData[i * 2];
                    uint64_t end   = queryData[i * 2 + 1];
                    uint64_t time  = (end < begin) ? 0 : end - begin;

					f(ProfilingEventData { m.name, time, indent, m.print });

                    ringbuffer.release(i);
                    i = ringbuffer.oldest();
                }
            }
        };

        struct DeviceState : std::enable_shared_from_this<DeviceState>
        {
            Adapter                     adapter;
            ComPtr<ID3D12Device>        device;
            ComPtr<ID3D12CommandQueue>  graphicsQueue;

            GrowingPool<std::shared_ptr<CommandListState>> freeGraphicsCommandLists;
            GPUProgressTracking progress;
            std::shared_ptr<UploadHeap> uploadHeap;
			std::shared_ptr<QueryHeap> queryHeap;

			std::vector<ProfilingEventData> profilingData;
			struct ProfilingEventHistory
			{
				std::vector<float> values;
				int next = 0;
                bool printToConsole = false;
			};
			int profilingDataHistoryLength = 10;
			std::unordered_map<uint64_t, ProfilingEventHistory> profilingDataHistory;
			std::vector<char> profilingDataHierarchicalName;

            ViewHeap rtvs;
            ViewHeap dsvs;
            ViewHeap shaderViews;
            Descriptor nullTextureSRV;
            Descriptor nullTextureUAV;

            std::shared_ptr<ShaderLoader> shaderLoader;
            std::unordered_map<info::PipelineKey, std::shared_ptr<PipelineState>> pipelines;

            struct ImGui
            {
                TextureSRV fontAtlas;
                GraphicsPipeline imguiRenderer;
            } imgui;
            int2 debugMousePosition;

            DeviceState(Adapter adapter_,
                        ComPtr<ID3D12Device> pDevice,
                        std::shared_ptr<backend::ShaderLoader> pShaderLoader);

            ~DeviceState();

            ViewHeap &viewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
            {
                switch (type)
                {
                case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
                    return rtvs;
                case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
                    return dsvs;
                case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
                    return shaderViews;
                default:
                    XOR_CHECK(false, "Unknown heap type.");
                    __assume(0);
                }
            }

            ID3D12Device *operator->() { return device.Get(); }
        };

    }
}

