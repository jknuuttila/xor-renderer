#include "Xor/Xor.hpp"
#include "Xor/Image.hpp"

#include "Core/TLog.hpp"

#include "ImguiRenderer.sig.h"

#include <unordered_map>
#include <unordered_set>

namespace xor
{
    namespace backend
    {
        static const char ShaderFileExtension[] = ".cso";
        static const uint MaxRTVs = 256;
        static const uint MaxDSVs = 256;
        static const uint DescriptorHeapSize = 65536 * 4;
        static const uint DescriptorHeapRing = 65536 * 3;

        class DeviceChild
        {
            std::weak_ptr<DeviceState> m_parentDevice;
        public:
            DeviceChild() = default;
            DeviceChild(std::weak_ptr<DeviceState> device)
                : m_parentDevice(device)
            {}

            void setParent(Device *device)
            {
                m_parentDevice = device->m_state;
            }

            Device device()
            {
                return Device(m_parentDevice.lock());
            }
        };

        static ComPtr<IDXGIFactory4> dxgiFactory()
        {
            ComPtr<IDXGIFactory4> factory;

            XOR_CHECK_HR(CreateDXGIFactory1(
                __uuidof(IDXGIFactory4),
                &factory));

            return factory;
        }

        static void setName(ComPtr<ID3D12Object> object, const String &name)
        {
            object->SetName(name.wideStr().c_str());
        }

#define XOR_INTERNAL_DEBUG_NAME(variable) setName(variable, #variable)

        static bool compileShader(const BuildInfo &shaderBuildInfo)
        {
            log("Pipeline", "Compiling shader %s\n", shaderBuildInfo.target.cStr());

            String output;
            String errors;

            int returnCode = shellCommand(
                shaderBuildInfo.buildExe,
                shaderBuildInfo.buildArgs,
                &output,
                &errors);

            if (output) log(nullptr, "%s", output.cStr());
            if (errors) log(nullptr, "%s", errors.cStr());

            return returnCode == 0;
        }

        struct ShaderBinary : public D3D12_SHADER_BYTECODE
        {
            std::vector<uint8_t> bytecode;

            ShaderBinary()
            {
                pShaderBytecode = nullptr;
                BytecodeLength  = 0;
            }

            ShaderBinary(const String &filename)
            {
                bytecode = File(filename).read();
                pShaderBytecode = bytecode.data();
                BytecodeLength  = bytecode.size();
            }
        };

        struct CompletionCallback
        {
            SeqNum seqNum = InvalidSeqNum;
            std::function<void()> f;

            CompletionCallback(SeqNum seqNum, std::function<void()> f)
                : seqNum(seqNum)
                , f(std::move(f))
            {}

            // Smallest seqNum goes first in a priority queue.
            bool operator<(const CompletionCallback &c) const
            {
                return seqNum > c.seqNum;
            }
        };

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

            void executeCommandList(CommandList &&cmd)
            {
                newestExecuted = std::max(newestExecuted, cmd.number());
                executedCommandLists.emplace_back(std::move(cmd));
            }

            void retireCommandLists()
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

            void waitUntilCompleted(SeqNum seqNum)
            {
                while (!hasCompleted(seqNum))
                {
                    auto &executed = executedCommandLists;
                    XOR_CHECK(!executed.empty(), "Nothing to wait for, deadlock!");
                    executed.front().waitUntilCompleted();
                }
            }

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

        struct Descriptor
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu     = { 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE gpu     = { 0 };
            D3D12_CPU_DESCRIPTOR_HANDLE staging = { 0 };
            D3D12_DESCRIPTOR_HEAP_TYPE  type    = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        };

        class ViewHeap
        {
            ComPtr<ID3D12DescriptorHeap> m_stagingHeap;
            ComPtr<ID3D12DescriptorHeap> m_heap;
            OffsetPool                   m_freeDescriptors;
            GPUMemoryRingbuffer          m_ring;
            uint                         m_ringStart = 0;
            D3D12_DESCRIPTOR_HEAP_TYPE   m_type      = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
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
                offset *= m_increment;

                Descriptor descriptor;

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
            static const size_t UploadHeapSize = 128 * 1024 * 1024;
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

        struct ShaderLoader
        {
            struct ShaderData
            {
                std::shared_ptr<const BuildInfo> buildInfo;
                std::unordered_map<PipelineState *, std::weak_ptr<PipelineState>> users;
                uint64_t timestamp = 0;

                void rebuildPipelines();
                bool isOutOfDate() const
                {
                    return timestamp < buildInfo->sourceTimestamp();
                }
            };

            std::unordered_map<String, std::shared_ptr<ShaderData>> shaderData;
            std::vector<String> shaderScanQueue;
            size_t shaderScanIndex = 0;

            void scanChangedSources();
            void registerBuildInfo(std::shared_ptr<const BuildInfo> buildInfo);

            void registerShaderTlog(StringView projectName,
                                    StringView shaderTlogPath);
        };

        struct CommandListState : DeviceChild
        {
            ComPtr<ID3D12CommandAllocator>    allocator;
            ComPtr<ID3D12GraphicsCommandList> cmd;

            uint64_t            timesStarted = 0;
            ComPtr<ID3D12Fence> timesCompleted;
            Handle              completedEvent;

            SeqNum seqNum = 0;
            bool closed = false;

            Texture activeRenderTarget;

            std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> cbvs;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvs;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> uavs;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> viewDescriptorSrcs;

            CommandListState(Device &dev)
            {
                setParent(&dev);

                XOR_CHECK_HR(dev.device()->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    __uuidof(ID3D12CommandAllocator),
                    &allocator));
                XOR_INTERNAL_DEBUG_NAME(allocator);

                XOR_CHECK_HR(dev.device()->CreateCommandList(
                    0,
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    allocator.Get(),
                    nullptr,
                    __uuidof(ID3D12GraphicsCommandList),
                    &cmd));
                XOR_INTERNAL_DEBUG_NAME(cmd);

                XOR_CHECK_HR(dev.device()->CreateFence(
                    0,
                    D3D12_FENCE_FLAG_NONE, 
                    __uuidof(ID3D12Fence),
                    &timesCompleted));
                XOR_INTERNAL_DEBUG_NAME(timesCompleted);

                completedEvent = CreateEventExA(nullptr, nullptr, 0, 
                                                EVENT_ALL_ACCESS);
                XOR_CHECK(!!completedEvent, "Failed to create completion event.");
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

            ViewHeap rtvs;
            ViewHeap dsvs;
            ViewHeap shaderViews;

            std::shared_ptr<ShaderLoader> shaderLoader;

            struct ImGui
            {
                TextureSRV fontAtlas;
                GraphicsPipeline imguiRenderer;
            } imgui;

            DeviceState(Adapter adapter_,
                        ComPtr<ID3D12Device> pDevice,
                        std::shared_ptr<backend::ShaderLoader> pShaderLoader)
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

                uploadHeap = std::make_shared<UploadHeap>(device.Get());

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
            }

            ~DeviceState()
            {
                progress.waitUntilDrained();
#if 0
                ComPtr<ID3D12DebugDevice> debug;
                XOR_CHECK_HR(device.As(&debug));
                debug->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
#endif
            }

            ViewHeap &viewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type)
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

            void releaseDescriptor(Descriptor descriptor)
            {
                viewHeap(descriptor.type).release(descriptor);
            }

            ID3D12Device *operator->() { return device.Get(); }
        };

        struct SwapChainState : DeviceChild
        {
            ComPtr<IDXGISwapChain3> swapChain;

            struct Backbuffer
            {
                SeqNum seqNum = InvalidSeqNum;
                TextureRTV rtvSRGB;
                TextureRTV rtvGamma;
            };
            std::vector<Backbuffer> backbuffers;

            ~SwapChainState()
            {
                device().waitUntilDrained();
            }
        };

        struct ResourceState : DeviceChild
        {
            ComPtr<ID3D12Resource> resource;
            mutable D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

            ~ResourceState()
            {
                // Actually release the resource once every command list that could possibly have
                // referenced it has retired.

                // Queue up a no-op lambda, that holds the resource ComPtr by value.
                // When the Device has executed it, it will get destroyed, freeing the last reference.
                if (auto dev = device())
                    dev.whenCompleted([resource = std::move(resource)]{});
            }
        };

        struct DescriptorViewState : DeviceChild
        {
            Descriptor descriptor;

            ~DescriptorViewState()
            {
                if (auto dev = device())
                {
                    dev.whenCompleted([dev, descriptor = descriptor]() mutable
                    {
                        dev.S().releaseDescriptor(descriptor);
                    });
                }
            }
        };

        struct RootSignature
        {
            ComPtr<ID3D12RootSignature> rs;
            unsigned numCBVs = 0;
            unsigned numSRVs = 0;
            unsigned numUAVs = 0;
        };

        struct PipelineState
            : std::enable_shared_from_this<PipelineState>
            , DeviceChild
        {
            std::shared_ptr<GraphicsPipeline::Info> graphicsInfo;
            ComPtr<ID3D12PipelineState> pso;
            RootSignature rootSignature;

            ShaderBinary loadShader(Device &device, StringView name)
            {
                if (!name)
                    return ShaderBinary();

                String shaderPath = File::canonicalize(name + ShaderFileExtension, true);

                auto &loader = *device.S().shaderLoader;
                if (auto data = loader.shaderData[shaderPath])
                {
                    if (data->timestamp == 0)
                        data->timestamp = data->buildInfo->targetTimestamp();

                    uint64_t sourceTimestamp = data->buildInfo->sourceTimestamp();

                    if (data->timestamp < sourceTimestamp)
                    {
                        compileShader(*data->buildInfo);
                        data->timestamp = sourceTimestamp;
                    }
                    else
                    {
                        log("Pipeline", "Shader has not been modified since last compile.\n");
                    }

                    data->users[this] = shared_from_this();
                }

                log("Pipeline", "Loading shader %s\n", shaderPath.cStr());
                return ShaderBinary(shaderPath);
            }

            void reload()
            {
                auto dev = device();

                log("Pipeline", "Rebuilding PSO.\n");

                D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = graphicsInfo->desc();

                ShaderBinary vs = loadShader(dev, graphicsInfo->m_vs);
                ShaderBinary ps = loadShader(dev, graphicsInfo->m_ps);

                if (graphicsInfo->m_vs)
                {
                    rootSignature = dev.collectRootSignature(vs);
                    desc.VS = vs;
                }
                else
                {
                    zero(desc.VS);
                }

                if (graphicsInfo->m_ps)
                {
                    rootSignature = dev.collectRootSignature(vs);
                    desc.PS = ps;
                }
                else
                {
                    zero(desc.PS);
                }

                releasePSO();

                XOR_CHECK_HR(dev.device()->CreateGraphicsPipelineState(
                    &desc,
                    __uuidof(ID3D12PipelineState),
                    &pso));
            }

            void releasePSO()
            {
                if (!pso)
                    return;

                if (auto dev = device())
                    dev.whenCompleted([pso = std::move(pso)]{});
            }

            ~PipelineState()
            {
                releasePSO();
            }
        };

        void ShaderLoader::scanChangedSources()
        {
            if (shaderScanQueue.empty())
                return;

            shaderScanIndex = (shaderScanIndex + 1) % shaderScanQueue.size();
            auto &shader = shaderScanQueue[shaderScanIndex];
            auto it = shaderData.find(shader);
            if (it != shaderData.end())
            {
                auto &data = *it->second;
                if (data.isOutOfDate())
                {
                    log("ShaderLoader",
                        "%s is out of date.\n",
                        data.buildInfo->target.cStr());

                    data.rebuildPipelines();
                }
            }
        }

        void ShaderLoader::registerBuildInfo(std::shared_ptr<const BuildInfo> buildInfo)
        {
            auto &shaderPath = buildInfo->target;
            auto &data = shaderData[shaderPath];

            if (!data)
            {
                data = std::make_shared<ShaderLoader::ShaderData>();
                shaderScanQueue.emplace_back(shaderPath);
                data->buildInfo = buildInfo;
                data->timestamp = data->buildInfo->targetTimestamp();
                log("ShaderLoader", "Registering shader %s for tracking.\n", shaderPath.cStr());
            }
        }

        void ShaderLoader::registerShaderTlog(StringView projectName, StringView shaderTlogPath)
        {
            for (auto &buildInfo : scanBuildInfos(shaderTlogPath, ShaderFileExtension))
                registerBuildInfo(buildInfo);
        }

        void ShaderLoader::ShaderData::rebuildPipelines()
        {
            std::vector<std::shared_ptr<PipelineState>> pipelinesToRebuild;
            pipelinesToRebuild.reserve(users.size());
            for (auto &kv : users)
            {
                if (auto p = kv.second.lock())
                    pipelinesToRebuild.emplace_back(std::move(p));
            }

            users.clear();
            for (auto &p : pipelinesToRebuild)
            {
                p->reload();
                users[p.get()] = p;
            }
        }

    }

    namespace info
    {
        BufferViewInfo BufferViewInfo::defaults(const BufferInfo & bufferInfo) const
        {
            BufferViewInfo info = *this;

            if (!info.format)
                info.format = bufferInfo.format;

            if (info.numElements == 0)
                info.numElements = static_cast<uint>(bufferInfo.size);

            return info;
        }

        uint BufferViewInfo::sizeBytes() const
        {
            return numElements * format.size();
        }

        BufferInfo BufferInfo::fromBytes(Span<const uint8_t> data, Format format)
        {
            XOR_ASSERT(data.size() % format.size() == 0,
                       "Initializer data size is not a multiple of the element type size.");

            auto numElements = data.size() / format.size();

            BufferInfo info(numElements, format);

            info.m_initializer = [data] (CommandList &cmd, Buffer &buf) 
            {
                cmd.updateBuffer(buf, data);
            };

            return info;
        }

        D3D12_INPUT_LAYOUT_DESC InputLayoutInfo::desc() const
        {
            D3D12_INPUT_LAYOUT_DESC d;
            d.NumElements        = static_cast<UINT>(m_elements.size());
            d.pInputElementDescs = m_elements.data();
            return d;
        }

        TextureInfo::TextureInfo(const Image & image, Format fmt)
        {
            size = image.size();

            if (fmt)
                format = fmt;
            else
                format = image.format();

            mipLevels = image.mipLevels();

            m_initializer = [&image] (CommandList &cmd, Texture &tex) 
            {
                auto mipLevels = image.mipLevels();
                for (uint m = 0; m < mipLevels; ++m)
                {
                    cmd.updateTexture(tex,
                                      image.subresource(m),
                                      Subresource(m));
                }
            };
        }

        TextureInfo::TextureInfo(const ImageData & data, Format fmt)
        {
            size = data.size;
            if (fmt)
                format = fmt;
            else
                format = data.format;

            m_initializer = [data] (CommandList &cmd, Texture &tex) 
            {
                cmd.updateTexture(tex, data);
            };
        }

        TextureInfo::TextureInfo(ID3D12Resource * texture)
        {
            auto desc = texture->GetDesc();
            XOR_CHECK(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                      "Expected a texture");
            size   = { static_cast<uint>(desc.Width), desc.Height };
            format = desc.Format;
        }

        size_t TextureInfo::sizeBytes() const
        {
            // FIXME: This is not accurate
            return format.rowSizeBytes(size.x) * size.y;
        }

        TextureViewInfo TextureViewInfo::defaults(const TextureInfo & textureInfo) const
        {
            TextureViewInfo info = *this;

            if (!info.format)
                info.format = textureInfo.format;

            return info;
        }
    }

    using namespace xor::backend;

    Xor::Xor(DebugLayer debugLayer)
    {
        if (debugLayer == DebugLayer::Default)
        {
#if defined(_DEBUG)
            debugLayer = DebugLayer::Enabled;
#else
            debugLayer = DebugLayer::Disabled;
#endif
        }

        if (debugLayer == DebugLayer::Enabled)
        {
            ComPtr<ID3D12Debug> debug;
            XOR_CHECK_HR(D3D12GetDebugInterface(
                __uuidof(ID3D12Debug),
                &debug));
            debug->EnableDebugLayer();
        }

        auto factory = dxgiFactory();

        m_shaderLoader = std::make_shared<ShaderLoader>();

        {
            uint i = 0;
            bool foundAdapters = true;
            while (foundAdapters)
            {
                ComPtr<IDXGIAdapter1> adapter;
                auto hr = factory->EnumAdapters1(i, &adapter);

                switch (hr)
                {
                case S_OK:
                    m_adapters.emplace_back();
                    {
                        auto &a = m_adapters.back();
                        XOR_CHECK_HR(adapter.As(&a.m_adapter));
                        DXGI_ADAPTER_DESC2 desc = {};
                        XOR_CHECK_HR(a.m_adapter->GetDesc2(&desc));
                        a.m_description  = String(desc.Description);
                        a.m_debug        = debugLayer == DebugLayer::Enabled;
                        a.m_shaderLoader = m_shaderLoader;
                    }
                    break;
                case DXGI_ERROR_NOT_FOUND:
                    foundAdapters = false;
                    break;
                default:
                    // This fails.
                    XOR_CHECK_HR(hr);
                    break;
                }

                ++i;
            }
        }
    }

    Xor::~Xor()
    {
    }

    Span<Adapter> Xor::adapters()
    {
        return asSpan(m_adapters);
    }

    Adapter & Xor::defaultAdapter()
    {
        XOR_CHECK(!m_adapters.empty(), "No adapters detected!");
        return m_adapters.front();
    }

    Device Xor::defaultDevice()
    {
        for (auto &adapter : m_adapters)
        {
            if (Device device = adapter.createDevice())
                return device;
        }

        XOR_CHECK(false, "Failed to find a Direct3D 12 device.");

        return Device();
    }

    void Xor::registerShaderTlog(StringView projectName, StringView shaderTlogPath)
    {
        m_shaderLoader->registerShaderTlog(projectName, shaderTlogPath);
    }

    Device Adapter::createDevice()
    {
        ComPtr<ID3D12Device> device;

        auto hr = D3D12CreateDevice(
            m_adapter.Get(),
            D3D_FEATURE_LEVEL_12_0,
            __uuidof(ID3D12Device),
            &device);

        if (FAILED(hr))
        {
            log("Adapter", "Failed to create device: %s\n", errorMessage(hr).cStr());
            return Device();
        }

        XOR_INTERNAL_DEBUG_NAME(device);

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (m_debug && device.As(&infoQueue) == S_OK)
        {
            XOR_CHECK_HR(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
            XOR_CHECK_HR(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      TRUE));
            XOR_CHECK_HR(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    TRUE));

            // The graphics debugger generates these, so disable.
            D3D12_MESSAGE_SEVERITY disabledSeverities[] = {
                D3D12_MESSAGE_SEVERITY_INFO,
            };
            D3D12_MESSAGE_ID disabledMessages[] = {
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
            };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumSeverities  = static_cast<uint>(size(disabledSeverities));
            filter.DenyList.NumIDs         = static_cast<uint>(size(disabledMessages));
            filter.DenyList.pSeverityList  = disabledSeverities;
            filter.DenyList.pIDList        = disabledMessages;
            XOR_CHECK_HR(infoQueue->PushStorageFilter(&filter));
        }

        return Device(*this,
                      std::move(device),
                      m_shaderLoader);
    }

    Device::Device(Adapter adapter,
                   ComPtr<ID3D12Device> device,
                   std::shared_ptr<backend::ShaderLoader> shaderLoader)
    {
        makeState(std::move(adapter), std::move(device), std::move(shaderLoader));

        S().shaderLoader->registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        {
            uint8_t *pixels = nullptr;
            int2 size;

            ImGuiIO &io = ImGui::GetIO();
            io.DeltaTime = 1.f/60.f;
            io.Fonts->AddFontDefault();
            io.Fonts->GetTexDataAsAlpha8(&pixels, &size.x, &size.y);

            ImGuiStyle &style = ImGui::GetStyle();
            style.Colors[ImGuiCol_FrameBg]        = float4(70, 70, 70, 77) / 255;
            style.Colors[ImGuiCol_FrameBgHovered] = float4(110, 110, 110, 102) / 255;
            style.Colors[ImGuiCol_FrameBgActive]  = float4(200, 70, 70, 102) / 255;

            ImageData data;
            data.size   = uint2(size);
            data.format = DXGI_FORMAT_R8_UNORM;
            data.setDefaultSizes();
            data.data = Span<const uint8_t>(pixels, data.sizeBytes());

            S().imgui.fontAtlas = createTextureSRV(Texture::Info(data));

            io.KeyMap[ImGuiKey_Tab]        = VK_TAB;
            io.KeyMap[ImGuiKey_LeftArrow]  = VK_LEFT;
            io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
            io.KeyMap[ImGuiKey_UpArrow]    = VK_UP;
            io.KeyMap[ImGuiKey_DownArrow]  = VK_DOWN;
            io.KeyMap[ImGuiKey_PageUp]     = VK_PRIOR;
            io.KeyMap[ImGuiKey_PageDown]   = VK_NEXT;
            io.KeyMap[ImGuiKey_Home]       = VK_HOME;
            io.KeyMap[ImGuiKey_End]        = VK_END;
            io.KeyMap[ImGuiKey_Delete]     = VK_DELETE;
            io.KeyMap[ImGuiKey_Backspace]  = VK_BACK;
            io.KeyMap[ImGuiKey_Enter]      = VK_RETURN;
            io.KeyMap[ImGuiKey_Escape]     = VK_ESCAPE;
            io.KeyMap[ImGuiKey_A]          = 'A';
            io.KeyMap[ImGuiKey_C]          = 'C';
            io.KeyMap[ImGuiKey_V]          = 'V';
            io.KeyMap[ImGuiKey_X]          = 'X';
            io.KeyMap[ImGuiKey_Y]          = 'Y';
            io.KeyMap[ImGuiKey_Z]          = 'Z';
        }

        S().imgui.imguiRenderer = createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("ImguiRenderer.vs")
            .pixelShader("ImguiRenderer.ps")
            .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM)
            .winding(false)
            .blend(0, true)
            .inputLayout(info::InputLayoutInfoBuilder()
                         .element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM)));
    }

    Device::Device(StatePtr state)
    {
        m_state = std::move(state);
    }

    SwapChain Device::createSwapChain(Window &window)
    {
        static const uint BufferCount = 2;

        auto factory = dxgiFactory();

        SwapChain swapChain;
        swapChain.makeState().setParent(this);

        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width              = window.size().x;
            desc.Height             = window.size().y;
            desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Stereo             = false;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount        = BufferCount;
            desc.Scaling            = DXGI_SCALING_NONE;
            desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags              = 0;

            ComPtr<IDXGISwapChain1> swapChain1;
            XOR_CHECK_HR(factory->CreateSwapChainForHwnd(
                S().graphicsQueue.Get(),
                window.hWnd(),
                &desc,
                nullptr,
                nullptr,
                &swapChain1));

            XOR_CHECK_HR(swapChain1.As(&swapChain.S().swapChain));
        }

        for (uint i = 0; i < BufferCount; ++i)
        {
            SwapChainState::Backbuffer bb;

            auto &tex = bb.rtvSRGB.m_texture;
            tex.makeState().setParent(this);
            XOR_CHECK_HR(swapChain.S().swapChain->GetBuffer(
                i,
                __uuidof(ID3D12Resource),
                &tex.S().resource));

            tex.makeInfo() = Texture::Info(tex.S().resource.Get());

            bb.rtvGamma.m_texture = bb.rtvSRGB.m_texture;

            bb.rtvSRGB.makeState().setParent(this);
            bb.rtvGamma.makeState().setParent(this);

            bb.rtvSRGB.S().descriptor  = S().rtvs.allocateFromHeap();
            bb.rtvGamma.S().descriptor = S().rtvs.allocateFromHeap();

            {
                D3D12_RENDER_TARGET_VIEW_DESC desc = {};
                desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                desc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice   = 0;
                desc.Texture2D.PlaneSlice = 0;

                device()->CreateRenderTargetView(
                    tex.S().resource.Get(),
                    &desc,
                    bb.rtvSRGB.S().descriptor.cpu);

                desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
                device()->CreateRenderTargetView(
                    tex.S().resource.Get(),
                    &desc,
                    bb.rtvGamma.S().descriptor.cpu);
            }

            swapChain.S().backbuffers.emplace_back(std::move(bb));
        }

        return swapChain;
    }

    GraphicsPipeline::Info::Info()
        : D3D12_GRAPHICS_PIPELINE_STATE_DESC {}
    {
        RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        RasterizerState.FrontCounterClockwise = TRUE;
        RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        RasterizerState.DepthClipEnable       = TRUE;

        DepthStencilState.DepthEnable    = FALSE;
        DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_ALWAYS;

        PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        // Depth disabled by default

        multisampling(1, 0);

        SampleMask = ~0u;
        for (auto &rt : BlendState.RenderTarget)
            rt.RenderTargetWriteMask = 0xf;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC GraphicsPipeline::Info::desc() const
    {
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::vertexShader(const String & vsName)
    {
        m_vs = vsName;
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::pixelShader(const String & psName)
    {
        m_ps = psName;
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::renderTargetFormats(Format format)
    {
        NumRenderTargets = 1;
        RTVFormats[0] = format;
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::renderTargetFormats(Span<const Format> formats)
    {
        NumRenderTargets = static_cast<uint>(formats.size());
        for (uint i = 0; i < NumRenderTargets; ++i)
            RTVFormats[i] = formats[i];
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::renderTargetFormats(Span<const DXGI_FORMAT> formats)
    {
        NumRenderTargets = static_cast<uint>(formats.size());
        for (uint i = 0; i < NumRenderTargets; ++i)
            RTVFormats[i] = formats[i];
        return *this;
    }

    GraphicsPipeline::Info & GraphicsPipeline::Info::depthFormat(Format format)
    {
        DSVFormat = format;
        return *this;
    }

    GraphicsPipeline::Info & GraphicsPipeline::Info::depthMode(info::DepthMode mode)
    {
        switch (mode)
        {
        case info::DepthMode::Disabled:
        default:
            DepthStencilState.DepthEnable    = FALSE;
            DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            break;
        case info::DepthMode::ReadOnly:
            DepthStencilState.DepthEnable    = TRUE;
            DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        case info::DepthMode::Write:
            DepthStencilState.DepthEnable    = TRUE;
            DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        }

        DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

        return *this;
    }

    GraphicsPipeline::Info & GraphicsPipeline::Info::inputLayout(const info::InputLayoutInfo & ilInfo)
    {
        // Put the input layout info object behind a pointer so the element addresses
        // do not change even if the pipeline info object is copied.
        m_inputLayout = std::make_shared<info::InputLayoutInfo>(ilInfo);
        InputLayout   = m_inputLayout->desc();
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::multisampling(uint samples, uint quality)
    {
        SampleDesc.Count   = samples;
        SampleDesc.Quality = quality;
        return *this;
    }

    GraphicsPipeline::Info & GraphicsPipeline::Info::topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
    {
        PrimitiveTopologyType = type;
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::fill(D3D12_FILL_MODE fillMode)
    {
        RasterizerState.FillMode = fillMode;
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::cull(D3D12_CULL_MODE cullMode)
    {
        RasterizerState.CullMode = cullMode;
        return *this;
    }

    GraphicsPipeline::Info &GraphicsPipeline::Info::winding(bool counterClockWise)
    {
        RasterizerState.FrontCounterClockwise = static_cast<BOOL>(counterClockWise);
        return *this;
    }

    GraphicsPipeline::Info & GraphicsPipeline::Info::blend(uint renderTarget, bool enabled, D3D12_BLEND src, D3D12_BLEND dst, D3D12_BLEND_OP op)
    {
        XOR_ASSERT(renderTarget < 8, "Invalid render target index");
        auto &rt = BlendState.RenderTarget[renderTarget];
        rt.BlendEnable    = static_cast<BOOL>(enabled);
        rt.LogicOpEnable  = FALSE;
        rt.BlendOp        = op;
        rt.SrcBlend       = src;
        rt.DestBlend      = dst;
        rt.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha  = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        return *this;
    }

    GraphicsPipeline Device::createGraphicsPipeline(const GraphicsPipeline::Info &info)
    {

        GraphicsPipeline pipeline;
        pipeline.makeState().setParent(this);
        pipeline.S().graphicsInfo = std::make_shared<GraphicsPipeline::Info>(info);
        pipeline.S().reload();
        return pipeline;
    }

    RootSignature Device::collectRootSignature(const D3D12_SHADER_BYTECODE &shader)
    { 
        RootSignature rs;

        XOR_CHECK_HR(device()->CreateRootSignature(
            0,
            shader.pShaderBytecode,
            shader.BytecodeLength,
            __uuidof(ID3D12RootSignature),
            &rs.rs));

        ComPtr<ID3D12RootSignatureDeserializer> deserializer;
        XOR_CHECK_HR(D3D12CreateRootSignatureDeserializer(
            shader.pShaderBytecode,
            shader.BytecodeLength,
            __uuidof(ID3D12RootSignatureDeserializer),
            &deserializer));
        auto desc = deserializer->GetRootSignatureDesc();

        for (uint i = 0; i < desc->NumParameters; ++i)
        {
            auto &p = desc->pParameters[i];

            if (p.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                continue;

            for (uint j = 0; j < p.DescriptorTable.NumDescriptorRanges; ++j)
            {
                auto &dr = p.DescriptorTable.pDescriptorRanges[j];
                switch (dr.RangeType)
                {
                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                    rs.numCBVs = dr.NumDescriptors;
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                    rs.numSRVs = dr.NumDescriptors;
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                    rs.numUAVs = dr.NumDescriptors;
                    break;
                default:
                    break;
                }
            }
        }

        return rs;
    }

    HeapBlock Device::uploadBytes(Span<const uint8_t> bytes, SeqNum cmdListNumber, uint alignment)
    {
        HeapBlock block;
        block.heap = S().uploadHeap->heap.Get();
        block.block = S().uploadHeap->uploadBytes(
            S().progress,
            bytes,
            cmdListNumber,
            alignment);
        return block;
    }

    ID3D12Device * Device::device()
    {
        return S().device.Get();
    }

    CommandList Device::initializerCommandList()
    {
        return graphicsCommandList();
    }

    Buffer Device::createBuffer(const Buffer::Info & info)
    {
        Buffer buffer;
        buffer.makeInfo() = info;
        buffer.makeState().setParent(this);

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type                 = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask     = 0;
        heap.VisibleNodeMask      = 0;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width              = info.sizeBytes();
        desc.Height             = 1;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        XOR_CHECK_HR(device()->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            buffer.S().state,
            nullptr,
            __uuidof(ID3D12Resource),
            &buffer.S().resource));

        if (info.m_initializer)
        {
            auto initCmd = initializerCommandList();
            info.m_initializer(initCmd, buffer);
            execute(initCmd);
        }

        return buffer;
    }

    BufferVBV Device::createBufferVBV(Buffer buffer, const BufferVBV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(buffer.info());

        BufferVBV vbv;

        vbv.m_buffer              = buffer;
        vbv.m_vbv.BufferLocation  = buffer.S().resource->GetGPUVirtualAddress();
        vbv.m_vbv.BufferLocation += info.firstElement * info.format.size();
        vbv.m_vbv.SizeInBytes     = info.sizeBytes();
        vbv.m_vbv.StrideInBytes   = info.format.size();

        return vbv;
    }

    BufferVBV Device::createBufferVBV(const Buffer::Info & bufferInfo, const BufferVBV::Info & viewInfo)
    {
        return createBufferVBV(createBuffer(bufferInfo), viewInfo);
    }

    BufferIBV Device::createBufferIBV(Buffer buffer, const BufferIBV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(buffer.info());

        BufferIBV ibv;

        ibv.m_buffer              = buffer;
        ibv.m_ibv.BufferLocation  = buffer.S().resource->GetGPUVirtualAddress();
        ibv.m_ibv.BufferLocation += info.firstElement * info.format.size();
        ibv.m_ibv.SizeInBytes     = info.sizeBytes();
        ibv.m_ibv.Format          = info.format;

        return ibv;
    }

    BufferIBV Device::createBufferIBV(const Buffer::Info & bufferInfo, const BufferIBV::Info & viewInfo)
    {
        return createBufferIBV(createBuffer(bufferInfo), viewInfo);
    }

    static D3D12_RESOURCE_FLAGS textureFlags(const Texture::Info &info)
    {
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

        if (info.format.isDepthFormat())
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        return flags;
    }

    Texture Device::createTexture(const Texture::Info & info)
    {
        Texture texture;
        texture.makeInfo() = info;
        texture.makeState().setParent(this);

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type                  = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask      = 0;
        heap.VisibleNodeMask       = 0;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment           = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width               = info.size.x;
        desc.Height              = info.size.y;
        desc.DepthOrArraySize    = 1;
        desc.MipLevels           = info.mipLevels;
        desc.Format              = info.format;
        desc.SampleDesc.Count    = 1;
        desc.SampleDesc.Quality  = 0;
        desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags               = textureFlags(info);

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = info.format;
        if (info.format.isDepthFormat())
        {
            clearValue.DepthStencil.Depth   = 0;
            clearValue.DepthStencil.Stencil = 0;
        }
        else
        {
            clearValue.Color[0] = 0;
            clearValue.Color[1] = 0;
            clearValue.Color[2] = 0;
            clearValue.Color[3] = 0;
        }

        bool hasClearValue =
            !!(desc.Flags &
                ( D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET));

        XOR_CHECK_HR(device()->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            texture.S().state,
            hasClearValue ? &clearValue : nullptr,
            __uuidof(ID3D12Resource),
            &texture.S().resource));

        if (info.m_initializer)
        {
            auto initCmd = initializerCommandList();
            info.m_initializer(initCmd, texture);
            execute(initCmd);
        }

        return texture;
    }

    TextureSRV Device::createTextureSRV(Texture texture, const TextureSRV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(texture.info());

        TextureSRV srv;
        srv.m_texture = texture;
        srv.makeState().setParent(this);
        srv.S().descriptor = S().shaderViews.allocateFromHeap();

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format                          = info.format;
        desc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MostDetailedMip       = 0;
        desc.Texture2D.MipLevels             = -1;
        desc.Texture2D.PlaneSlice            = 0;
        desc.Texture2D.ResourceMinLODClamp   = 0;

        device()->CreateShaderResourceView(
            texture.get(),
            &desc,
            srv.S().descriptor.staging);

        return srv;
    }

    TextureSRV Device::createTextureSRV(const Texture::Info & textureInfo, const TextureSRV::Info & viewInfo)
    {
        return createTextureSRV(createTexture(textureInfo), viewInfo);
    }

    TextureDSV Device::createTextureDSV(Texture texture, const TextureDSV::Info & viewInfo)
    {
        auto info = viewInfo.defaults(texture.info());

        TextureDSV dsv;
        dsv.m_texture = texture;
        dsv.makeState().setParent(this);
        dsv.S().descriptor = S().dsvs.allocateFromHeap();

        D3D12_DEPTH_STENCIL_VIEW_DESC desc   = {};
        desc.Format                          = info.format;
        desc.ViewDimension                   = D3D12_DSV_DIMENSION_TEXTURE2D;
        desc.Flags                           = D3D12_DSV_FLAG_NONE;
        desc.Texture2D.MipSlice              = 0;

        device()->CreateDepthStencilView(
            texture.get(),
            &desc,
            dsv.S().descriptor.cpu);

        return dsv;
    }

    TextureDSV Device::createTextureDSV(const Texture::Info & textureInfo, const TextureDSV::Info & viewInfo)
    {
        return createTextureDSV(createTexture(textureInfo), viewInfo);
    }

    CommandList Device::graphicsCommandList()
    {
        CommandList cmd = S().freeGraphicsCommandLists.allocate([this]
        {
            return std::make_shared<CommandListState>(*this);
        });

        cmd.reset();
        ID3D12DescriptorHeap *heaps[] = { S().shaderViews.get() };
        cmd.cmd()->SetDescriptorHeaps(1, heaps);

        ++cmd.S().timesStarted;
        cmd.S().seqNum = S().progress.startNewCommandList();

        return cmd;
    }

    void Device::execute(CommandList &cmd)
    {
        cmd.close();

        ID3D12CommandList *cmds[] = { cmd.S().cmd.Get() };
        S().graphicsQueue->ExecuteCommandLists(1, cmds);
        S().graphicsQueue->Signal(cmd.S().timesCompleted.Get(), cmd.S().timesStarted);

        S().progress.executeCommandList(std::move(cmd));
    }

    void Device::present(SwapChain & swapChain, bool vsync)
    {
        auto &backbuffer = swapChain.S().backbuffers[swapChain.currentIndex()];

        {
            auto toPresent = graphicsCommandList();
            toPresent.transition(backbuffer.rtvSRGB.m_texture, D3D12_RESOURCE_STATE_PRESENT);
            execute(toPresent);
        }

        // The backbuffer is assumed to depend on all command lists
        // that have been executed, but not on those which have
        // been started but not executed. Otherwise, deadlock could result.
        backbuffer.seqNum = S().progress.newestExecuted;
        swapChain.S().swapChain->Present(vsync ? 1 : 0, 0);
        S().shaderLoader->scanChangedSources();
        S().progress.retireCommandLists();
    }

    Device::ImguiInput Device::imguiInput(const Input & input)
    {
        ImGuiIO &io = ImGui::GetIO();

        if (!input.mouseMovements.empty())
            io.MousePos = float2(input.mouseMovements.back().position);

        for (auto &w : input.mouseWheel)
        {
            if (w.delta > 0)
                io.MouseWheel += 1;
            else if (w.delta < 0)
                io.MouseWheel -= 1;
        }

        for (auto &k : input.keyEvents)
        {
            switch (k.code)
            {
            case VK_LBUTTON:
                io.MouseDown[0] = k.pressed; break;
            case VK_RBUTTON:
                io.MouseDown[1] = k.pressed; break;
            case VK_MBUTTON:
                io.MouseDown[2] = k.pressed; break;
            default:
                if (k.code < 512)
                    io.KeysDown[k.code] = k.pressed;
                break;
            }
        }

        for (auto ch : input.characterInput)
            io.AddInputCharacter(static_cast<ImWchar>(ch));

        io.KeyCtrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        io.KeyShift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        io.KeyAlt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
        io.KeySuper = false;

        ImguiInput retval;
        retval.wantsKeyboard = io.WantCaptureKeyboard;
        retval.wantsMouse    = io.WantCaptureMouse;
        retval.wantsText     = io.WantTextInput;
        return retval;
    }

    SeqNum Device::now()
    {
        return S().progress.now();
    }

    void Device::whenCompleted(std::function<void()> f)
    {
        whenCompleted(std::move(f), now());
    }

    void Device::whenCompleted(std::function<void()> f, SeqNum seqNum)
    {
        S().progress.whenCompleted(std::move(f), seqNum);
    }

    bool Device::hasCompleted(SeqNum seqNum)
    {
        return S().progress.hasCompleted(seqNum);
    }

    void Device::waitUntilCompleted(SeqNum seqNum)
    {
        S().progress.waitUntilCompleted(seqNum);
    }

    void Device::waitUntilDrained()
    {
        S().progress.waitUntilDrained();
    }

    ID3D12GraphicsCommandList *CommandList::cmd()
    {
        return S().cmd.Get();
    }

    void CommandList::close()
    {
        if (!S().closed)
        {
            XOR_CHECK_HR(cmd()->Close());
            S().closed             = true;
            S().activeRenderTarget = Texture();
        }
    }

    void CommandList::reset()
    {
        if (S().closed)
        {
            XOR_CHECK_HR(cmd()->Reset(S().allocator.Get(), nullptr));
            S().closed             = false;
            S().activeRenderTarget = Texture();
        }
    }

    bool CommandList::hasCompleted()
    {
        auto completed = S().timesCompleted->GetCompletedValue();

        XOR_ASSERT(completed <= S().timesStarted,
                   "Command list completion count out of sync.");

        return completed == S().timesStarted;
    }

    void CommandList::waitUntilCompleted(DWORD timeout)
    {
        while (!hasCompleted())
        {
            XOR_CHECK_HR(S().timesCompleted->SetEventOnCompletion(
                S().timesStarted,
                S().completedEvent.get()));
            WaitForSingleObject(S().completedEvent.get(), timeout);
        }
    }

    CommandList::CommandList(StatePtr state)
    {
        m_state = std::move(state);
    }

    void CommandList::release()
    {
        // FIXME: This is a race condition :(
        if (m_state)
        {
            if (m_state.unique())
            {
                auto &dev = S().device().S();
                dev.freeGraphicsCommandLists.release(
                    std::move(m_state));
            }
            else
            {
                m_state.reset();
            }
        }
    }

    // FIXME: This is horribly inefficient and bad
    void CommandList::transition(const backend::Resource & resource, D3D12_RESOURCE_STATES newState)
    {
        if (!resource)
            return;

        auto &s = resource.S().state;

        if (s == newState)
            return;

        D3D12_RESOURCE_BARRIER barrier;

        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = resource.get();
        barrier.Transition.StateBefore = s;
        barrier.Transition.StateAfter  = newState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        cmd()->ResourceBarrier(1, &barrier);

        s = newState;
    }

    void CommandList::setupRootArguments()
    {
        auto &cbvs = S().cbvs;
        auto &srvs = S().srvs;
        auto &uavs = S().uavs;
        auto numCBVs = cbvs.size();
        auto numSRVs = srvs.size();
        auto numUAVs = uavs.size();
        auto totalDescriptors = numCBVs + numSRVs + numUAVs;

        if (totalDescriptors > 0)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE table;

            auto dev = S().device();
            auto &heap = dev.S().viewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            auto start = heap.allocateFromRing(dev.S().progress, totalDescriptors, number());

            auto &srcs = S().viewDescriptorSrcs;

            srcs.clear();
            srcs.reserve(totalDescriptors);

            table = heap.descriptorAtOffset(start).gpu;
            for (size_t c = 0; c < numCBVs; ++c)
            {
                dev.device()->CreateConstantBufferView(
                    &cbvs[c],
                    heap.descriptorAtOffset(start + c).cpu);
            }

            for (size_t s = 0; s < numSRVs; ++s)
                srcs.emplace_back(srvs[s]);

            for (size_t u = 0; u < numUAVs; ++u)
                srcs.emplace_back(uavs[u]);

            if (!srcs.empty())
            {
                auto dst = heap.descriptorAtOffset(start + numCBVs);
                uint amount[] ={ static_cast<uint>(srcs.size()) };
                dev.device()->CopyDescriptors(
                    1, &dst.cpu, amount,
                    amount[0], srcs.data(), nullptr,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }

            cmd()->SetGraphicsRootDescriptorTable(0, table);
        }
    }

    backend::HeapBlock CommandList::uploadBytes(Span<const uint8_t> bytes, uint alignment)
    {
        return device().uploadBytes(bytes, number(), alignment);
    }

    CommandList::~CommandList()
    {
        release();
    }

    CommandList::CommandList(CommandList && c)
    {
        m_state = std::move(c.m_state);
    }

    CommandList & CommandList::operator=(CommandList && c)
    {
        if (this != &c)
        {
            release();
            m_state = std::move(c.m_state);
        }
        return *this;
    }

    SeqNum CommandList::number() const
    {
        return S().seqNum;
    }

    Device CommandList::device()
    {
        return S().device();
    }

    void CommandList::bind(GraphicsPipeline &pipeline)
    {
        cmd()->SetGraphicsRootSignature(pipeline.S().rootSignature.rs.Get());
        cmd()->SetPipelineState(pipeline.S().pso.Get());

        S().cbvs.clear();
        S().srvs.clear();
        S().uavs.clear();

        S().cbvs.resize(pipeline.S().rootSignature.numCBVs);
        S().srvs.resize(pipeline.S().rootSignature.numSRVs);
        S().uavs.resize(pipeline.S().rootSignature.numUAVs);
    }

    void CommandList::clearRTV(TextureRTV &rtv, float4 color)
    {
        transition(rtv.m_texture, D3D12_RESOURCE_STATE_RENDER_TARGET);

        cmd()->ClearRenderTargetView(rtv.S().descriptor.cpu,
                                     color.data(),
                                     0, nullptr);
    }

    void CommandList::clearDSV(TextureDSV & dsv, float depth)
    {
        transition(dsv.m_texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        cmd()->ClearDepthStencilView(dsv.S().descriptor.cpu,
                                     D3D12_CLEAR_FLAG_DEPTH,
                                     depth,
                                     0,
                                     0, nullptr);
    }

    void CommandList::setViewport(uint2 size)
    {
        setViewport(size, { 0, int2(size) });
    }

    void CommandList::setViewport(uint2 size, Rect scissor)
    {
        D3D12_VIEWPORT viewport = {};

        viewport.Width    = static_cast<float>(size.x);
        viewport.Height   = static_cast<float>(size.y);
        viewport.MinDepth = D3D12_MIN_DEPTH;
        viewport.MaxDepth = D3D12_MAX_DEPTH;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;

        cmd()->RSSetViewports(1, &viewport);

        setScissor(scissor);
    }

    void CommandList::setScissor(Rect scissor)
    {
        D3D12_RECT scissorRect = {};

        scissorRect.left   = static_cast<LONG>(scissor.leftTop.x);
        scissorRect.top    = static_cast<LONG>(scissor.leftTop.y);
        scissorRect.right  = static_cast<LONG>(scissor.rightBottom.x);
        scissorRect.bottom = static_cast<LONG>(scissor.rightBottom.y);

        cmd()->RSSetScissorRects(1, &scissorRect);
    }

    void CommandList::setRenderTargets()
    {
        S().activeRenderTarget = Texture();

        cmd()->OMSetRenderTargets(0,
                                  nullptr,
                                  false,
                                  nullptr);
    }

    void CommandList::setRenderTargets(TextureRTV &rtv)
    {
        transition(rtv.m_texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
        S().activeRenderTarget = rtv.m_texture;

        cmd()->OMSetRenderTargets(1,
                                  &rtv.S().descriptor.cpu,
                                  FALSE,
                                  nullptr);

        setViewport(rtv.texture()->size);
    }

    void CommandList::setRenderTargets(TextureRTV & rtv, TextureDSV & dsv)
    {
        transition(rtv.m_texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(dsv.m_texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        S().activeRenderTarget = rtv.m_texture;

        cmd()->OMSetRenderTargets(1,
                                  &rtv.S().descriptor.cpu,
                                  FALSE,
                                  &dsv.S().descriptor.cpu);

        setViewport(rtv.texture()->size);
    }

    BufferVBV CommandList::dynamicBufferVBV(Span<const uint8_t> bytes, uint stride)
    {
        auto block = uploadBytes(bytes);

        BufferVBV vbv;
        vbv.m_vbv.BufferLocation  = block.heap->GetGPUVirtualAddress();
        vbv.m_vbv.BufferLocation += block.block.begin;
        vbv.m_vbv.SizeInBytes     = static_cast<uint>(bytes.size());
        vbv.m_vbv.StrideInBytes   = stride;
        return vbv;
    }

    BufferIBV CommandList::dynamicBufferIBV(Span<const uint8_t> bytes, Format format)
    {
        auto block = uploadBytes(bytes);

        BufferIBV ibv;
        ibv.m_ibv.BufferLocation  = block.heap->GetGPUVirtualAddress();
        ibv.m_ibv.BufferLocation += block.block.begin;
        ibv.m_ibv.SizeInBytes     = static_cast<uint>(bytes.size());
        ibv.m_ibv.Format          = format;
        return ibv;
    }

    void CommandList::setVBV(const BufferVBV & vbv)
    {
        transition(vbv.m_buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        cmd()->IASetVertexBuffers(0, 1, &vbv.m_vbv);
    }

    void CommandList::setVBVs(Span<const BufferVBV> vbvs)
    {
        uint slot = 0;
        for (auto &vbv : vbvs)
        {
            transition(vbv.m_buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            cmd()->IASetVertexBuffers(slot, 1, &vbv.m_vbv);
            ++slot;
        }
    }

    void CommandList::setIBV(const BufferIBV & ibv)
    {
        transition(ibv.m_buffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        cmd()->IASetIndexBuffer(&ibv.m_ibv);
    }

    void CommandList::setShaderView(unsigned slot, const TextureSRV & srv)
    {
        transition(srv.m_texture,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        auto &srvs = S().srvs;
        srvs[slot] = srv.S().descriptor.staging;
    }

    void CommandList::setConstantBuffer(unsigned slot, Span<const uint8_t> bytes)
    {
        auto block = uploadBytes(bytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        auto &cbvs = S().cbvs;
        cbvs[slot].BufferLocation = block.heap->GetGPUVirtualAddress() + block.block.begin;
        cbvs[slot].SizeInBytes    = roundUpToMultiple<uint>(
            static_cast<uint>(bytes.sizeBytes()),
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }

    void CommandList::setTopology(D3D_PRIMITIVE_TOPOLOGY topology)
    {
        cmd()->IASetPrimitiveTopology(topology);
    }

    void CommandList::draw(uint vertices, uint startVertex)
    {
        transition(S().activeRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        setupRootArguments();
        cmd()->DrawInstanced(vertices, 1, startVertex, 0);
    }

    void CommandList::drawIndexed(uint indices, uint startIndex)
    {
        transition(S().activeRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        setupRootArguments();
        cmd()->DrawIndexedInstanced(indices, 1, startIndex, 0, 0); 
    }

    void CommandList::updateBuffer(Buffer & buffer,
                                   Span<const uint8_t> data,
                                   size_t offset)
    {
        auto block = uploadBytes(data, 1);

        transition(buffer, D3D12_RESOURCE_STATE_COPY_DEST);
        S().cmd->CopyBufferRegion(
            buffer.get(),
            offset,
            block.heap,
            static_cast<UINT64>(block.block.begin),
            block.block.size());
    }

    void CommandList::updateTexture(Texture & texture, ImageData data, ImageRect dstPos)
    {
        auto block = uploadBytes(data.data, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource                   = texture.get();
        dst.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex            = dstPos.subresource.index(texture->mipLevels);

        D3D12_TEXTURE_COPY_LOCATION src        = {};
        src.pResource                          = block.heap;
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = static_cast<UINT64>(block.block.begin);
        src.PlacedFootprint.Footprint.Format   = data.format;
        src.PlacedFootprint.Footprint.Width    = data.size.x;
        src.PlacedFootprint.Footprint.Height   = data.size.y;
        src.PlacedFootprint.Footprint.Depth    = 1;
        src.PlacedFootprint.Footprint.RowPitch = data.pitch;

        transition(texture, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd()->CopyTextureRegion(
            &dst,
            dstPos.leftTop.x, dstPos.leftTop.y, 0,
            &src,
            nullptr);
    }

    void CommandList::copyTexture(Texture & dst, ImageRect dstPos,
                                  const Texture & src, ImageRect srcRect)
    {
        transition(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(dst, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource                   = dst.get();
        dstLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex            = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource                   = src.get();
        srcLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex            = 0;

        D3D12_BOX srcBox = {};
        srcBox.left      = srcRect.leftTop.x;
        srcBox.right     = srcRect.rightBottom.x;
        srcBox.top       = srcRect.leftTop.y;
        srcBox.bottom    = srcRect.rightBottom.y;
        srcBox.front     = 0;
        srcBox.back      = 1;

        cmd()->CopyTextureRegion(
            &dstLocation, dstPos.leftTop.x, dstPos.leftTop.y, 0,
            &srcLocation, srcRect.empty() ? nullptr : &srcBox);
    }

    void CommandList::imguiBeginFrame(SwapChain &swapChain, double deltaTime)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = float2(swapChain.backbuffer(false).m_texture->size);
        io.DeltaTime   = static_cast<float>(deltaTime);

        static HCURSOR arrow = LoadCursorA(nullptr, IDC_ARROW);

        if (io.MouseDrawCursor)
            SetCursor(nullptr);
        else
            SetCursor(arrow);

        ImGui::NewFrame();
    }

    void CommandList::imguiEndFrame(SwapChain &swapChain)
    {
        auto rtv = swapChain.backbuffer(false);

        ImGui::Render();

        auto drawData = ImGui::GetDrawData();
        XOR_ASSERT(drawData->Valid, "ImGui draw data is invalid!");

        auto &imgui = device().S().imgui;

        setRenderTargets(rtv);
        bind(imgui.imguiRenderer);

        uint2 resolution = rtv.texture()->size;
        int4 prevClipRect = -1;

        ImguiRenderer::Constants constants;
        constants.reciprocalResolution = 1.f / float2(resolution);

        for (int i = 0; i < drawData->CmdListsCount; ++i)
        {
            auto list = drawData->CmdLists[i];
            auto vbv = dynamicBufferVBV(asConstSpan(list->VtxBuffer));
            auto ibv = dynamicBufferIBV(asConstSpan(list->IdxBuffer));
            setVBV(vbv);
            setIBV(ibv);
            setTopology();

            uint indexOffset  = 0;
            for (auto &d : list->CmdBuffer)
            {
                int4 clipRect = int4(float4(d.ClipRect));
                // TODO: swizzle
                if (any(clipRect != prevClipRect))
                    setScissor({ int2(clipRect.x, clipRect.y), int2(clipRect.z, clipRect.w) });

                setConstants(constants);
                setShaderView(ImguiRenderer::tex, imgui.fontAtlas);
                drawIndexed(d.ElemCount, indexOffset);

                indexOffset += d.ElemCount;
            }
        }

        setRenderTargets();
    }

    uint SwapChain::currentIndex()
    {
        // Block until the current backbuffer has finished rendering.
        for (;;)
        {
            uint index = S().swapChain->GetCurrentBackBufferIndex();

            auto device = S().device();

            auto &cur = S().backbuffers[index];
            if (cur.seqNum < 0 || device.hasCompleted(cur.seqNum))
                return index;

            // If we got here, the backbuffer was presented, but hasn't finished yet.
            device.waitUntilCompleted(cur.seqNum);
        }
    }

    TextureRTV SwapChain::backbuffer(bool sRGB)
    {
        auto &bb = S().backbuffers[currentIndex()];
        return sRGB ? bb.rtvSRGB : bb.rtvGamma;
    }

    Texture TextureView::texture()
    {
        return m_texture;
    }

    ID3D12Resource *Resource::get() const
    {
        return m_state ? S().resource.Get() : nullptr;
    }
}
