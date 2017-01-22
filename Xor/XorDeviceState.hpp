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

            bool hasBeenExecuted(SeqNum seqNum);

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

        class GPUTransientMemoryAllocator
        {
        private:
            int64_t m_size      = 0;
            int64_t m_chunkSize = 0;

            using ChunkNumber = int64_t;
            std::vector<ChunkNumber>                    m_freeChunks;
            std::vector<std::pair<SeqNum, ChunkNumber>> m_usedChunks;

            String m_name;
        public:
            GPUTransientMemoryAllocator() = default;
            GPUTransientMemoryAllocator(size_t size, size_t chunkSize,
                                        String name = String());

            size_t size() const { return static_cast<size_t>(m_size); }

            Block allocate(GPUProgressTracking &progress, GPUTransientChunk &chunk,
                           size_t size, size_t alignment, SeqNum cmdList);

            ChunkNumber findFreeChunk(GPUProgressTracking &progress);
        };

        class ViewHeap
        {
            ComPtr<ID3D12DescriptorHeap> m_stagingHeap;
            ComPtr<ID3D12DescriptorHeap> m_heap;
            OffsetPool                   m_freeDescriptors;
            GPUTransientMemoryAllocator  m_transientAllocator;
            GPUTransientChunk            m_transientChunk;
            uint                         m_transientStart = 0;
            D3D12_DESCRIPTOR_HEAP_TYPE   m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

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

            static constexpr uint NumChunks = 64;
        public:

            ViewHeap() = default;
            ViewHeap(ID3D12Device *device,
                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                     const String &name,
                     uint totalSize,
                     uint transientSize = 0)
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

                m_transientStart     = totalSize - transientSize;
                m_freeDescriptors    = OffsetPool(m_transientStart);

                if (transientSize)
                    m_transientAllocator = GPUTransientMemoryAllocator(transientSize, transientSize / NumChunks, "ViewHeap");

                m_increment          = device->GetDescriptorHandleIncrementSize(m_type);
                m_cpuStart           = m_heap->GetCPUDescriptorHandleForHeapStart();
                m_gpuStart           = m_heap->GetGPUDescriptorHandleForHeapStart();
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

            int64_t allocateFromTransient(GPUProgressTracking &progress, size_t amount, SeqNum cmdList)
            {
                XOR_ASSERT(m_transientAllocator.size() > 0, "This ViewHeap has no transient portion.");
                return m_transientAllocator.allocate(progress, m_transientChunk,
                                                     amount, 1,
                                                     cmdList).begin + m_transientStart;
            }

            void release(Descriptor descriptor)
            {
                XOR_ASSERT(descriptor.type == m_type, "Released descriptor to the wrong heap.");
                size_t offset = descriptor.cpu.ptr - m_cpuStart.ptr;
                offset /= m_increment;
                XOR_ASSERT(offset < m_transientStart, "Released descriptor out of bounds.");
                m_freeDescriptors.release(static_cast<int64_t>(offset));
            }
        };

        template <D3D12_HEAP_TYPE HeapType>
        struct CPUVisibleHeap
        {
            static_assert(HeapType == D3D12_HEAP_TYPE_UPLOAD || HeapType == D3D12_HEAP_TYPE_READBACK,
                          "Unsupported heap type");

            static constexpr size_t HeapSize = 32 * 1024 * 1024;
            static constexpr size_t ChunkSize = 2 * 1024 * 1024;
            static constexpr bool IsUploadHeap   = HeapType == D3D12_HEAP_TYPE_UPLOAD;
            static constexpr bool IsReadbackHeap = HeapType == D3D12_HEAP_TYPE_READBACK;

            MovingPtr<GPUProgressTracking *> progress;
            ComPtr<ID3D12Resource> heap;
            GPUTransientMemoryAllocator allocator;
            MovingPtr<uint8_t *> mapped;
            bool flushed = false;

            CPUVisibleHeap(ID3D12Device *device, GPUProgressTracking &progress)
                : progress(&progress)
                , allocator(HeapSize, ChunkSize, String::format("CPUVisibleHeap<%d>", static_cast<int>(HeapType)))
            {
                D3D12_HEAP_PROPERTIES heapDesc = {};
                heapDesc.Type                 = HeapType;
                heapDesc.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                heapDesc.CreationNodeMask     = 0;
                heapDesc.VisibleNodeMask      = 0;

                D3D12_RESOURCE_DESC desc = {};
                desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
                desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                desc.Width              = HeapSize;
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
                    heapState(),
                    nullptr,
                    __uuidof(ID3D12Resource),
                    &heap));
                setName(heap, IsUploadHeap
                        ? "Main upload heap"
                        : "Main readback heap");

                mapHeap();
            }
            
            ~CPUVisibleHeap()
            {
                heap->Unmap(0, nullptr);
            }

            static D3D12_RESOURCE_STATES heapState()
            {
                if (IsUploadHeap)
                    return D3D12_RESOURCE_STATE_GENERIC_READ;
                else if (IsReadbackHeap)
                    return D3D12_RESOURCE_STATE_COPY_DEST;
                else
                    return D3D12_RESOURCE_STATE_COMMON;
            }

            void mapHeap()
            {
                void *p = nullptr;
                if (IsUploadHeap)
                {
                    D3D12_RANGE dontRead;
                    dontRead.Begin = 0;
                    dontRead.End = 0;
                    XOR_CHECK_HR(heap->Map(0, &dontRead, &p));
                }
                else
                {
                    XOR_CHECK_HR(heap->Map(0, nullptr, &p));
                }
                mapped = reinterpret_cast<uint8_t *>(p);
            }

            void flushHeap()
            {
                if (flushed)
                    return;

                flushBlock(Block(0, static_cast<int64_t>(allocator.size())));
                flushed = true;
            }

            void flushBlock(Block block)
            {
                if (IsUploadHeap)
                {
                    D3D12_RANGE flushRange;
                    flushRange.Begin = static_cast<size_t>(block.begin);
                    flushRange.End = static_cast<size_t>(block.end);
                    heap->Unmap(0, &flushRange);
                }
                else
                {
                    heap->Unmap(0, nullptr);
                }

                mapHeap();
            }

            Block uploadBytes(Span<const uint8_t> bytes,
                              SeqNum cmdListNumber, GPUTransientChunk &chunk,
                              uint alignment = 1)
            {
                XOR_CHECK(IsUploadHeap, "Cannot upload to non-upload heaps");
                auto block = allocator.allocate(*progress, chunk,
                                                bytes.sizeBytes(), alignment,
                                                cmdListNumber);
                memcpy(mapped + block.begin, bytes.data(), bytes.sizeBytes());
                flushed = false;
                flushBlock(block);
                return block;
            }

            Block readbackBytes(SeqNum cmdListNumber, GPUTransientChunk &chunk,
                                size_t bytes,
                                uint alignment = 1)
            {
                XOR_CHECK(IsReadbackHeap, "Cannot readback from non-readback heaps");
                auto block = allocator.allocate(*progress, chunk,
                                                bytes, alignment,
                                                cmdListNumber);
                flushed = false;
                return block;
            }
        };

        using UploadHeap   = CPUVisibleHeap<D3D12_HEAP_TYPE_UPLOAD>;
        using ReadbackHeap = CPUVisibleHeap<D3D12_HEAP_TYPE_READBACK>;

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
            std::shared_ptr<CPUVisibleHeap<D3D12_HEAP_TYPE_UPLOAD>>   uploadHeap;
            std::shared_ptr<CPUVisibleHeap<D3D12_HEAP_TYPE_READBACK>> readbackHeap;
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
            bool debugPrintEnabled = true;
            uint4 debugFeedbackValue;

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

