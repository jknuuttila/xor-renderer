#include "Xor.hpp"

#include "Core/TLog.hpp"

#include <unordered_map>
#include <unordered_set>

namespace xor
{
    namespace backend
    {
        static const char ShaderFileExtension[] = ".cso";

        class DeviceChild
        {
            std::weak_ptr<DeviceState> m_parentDevice;
        public:
            DeviceChild() = default;
            DeviceChild(std::weak_ptr<DeviceState> device)
                : m_parentDevice(device)
            {}

            void setParent(Device &device);
            void setParent(std::weak_ptr<DeviceState> device);

            Device device();
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

            Block allocate(size_t amount, SeqNum cmdList)
            {
                Metadata m;
                m.block = m_memoryRing.allocateBlock(amount);

                if (m.block.begin < 0)
                    return Block();

                m.allocatedBy = cmdList;

                auto metadataOffset = m_metadataRing.allocate();
                XOR_ASSERT(metadataOffset >= 0,
                           "Out of metadata space, increase ringbuffer size.");

                m_metadata[metadataOffset] = m;

                return m.block;
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

        static const uint MaxRTVs = 256;

        struct Descriptor
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu  = { 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE gpu  = { 0 };
            D3D12_DESCRIPTOR_HEAP_TYPE  type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        };

        struct ViewHeap
        {
            ComPtr<ID3D12DescriptorHeap> heap;
            OffsetPool freeDescriptors;

            uint increment                  = 0;
            D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

            ViewHeap() = default;
            ViewHeap(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_DESC desc)
            {
                type = desc.Type;

                XOR_CHECK_HR(device->CreateDescriptorHeap(
                    &desc,
                    __uuidof(ID3D12DescriptorHeap),
                    &heap));

                freeDescriptors = OffsetPool(desc.NumDescriptors);
                increment       = device->GetDescriptorHandleIncrementSize(type);
            }

            Descriptor allocate()
            {
                auto offset = freeDescriptors.allocate();
                XOR_CHECK(offset >= 0, "Ran out of descriptors in the heap.");

                offset *= increment;

                Descriptor descriptor;

                descriptor.cpu = heap->GetCPUDescriptorHandleForHeapStart();
                descriptor.gpu = heap->GetGPUDescriptorHandleForHeapStart();

                descriptor.cpu.ptr += offset;
                descriptor.gpu.ptr += offset;
                descriptor.type     = type;

                return descriptor;
            }

            void release(Descriptor descriptor)
            {
                XOR_ASSERT(descriptor.type == type, "Released descriptor to the wrong heap.");

                auto start = heap->GetCPUDescriptorHandleForHeapStart();
                size_t offset = descriptor.cpu.ptr - start.ptr;
                offset /= increment;
                freeDescriptors.release(static_cast<int64_t>(offset));
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
                auto size = bytes.sizeBytes() + (alignment - 1);
                auto block = ringbuffer.allocate(size, cmdListNumber);

                while (!block)
                {
                    auto oldest = ringbuffer.oldestCmdList();

                    XOR_CHECK(oldest >= 0, "Ringbuffer not big enough to hold %llu bytes.",
                              static_cast<llu>(bytes.sizeBytes()));

                    progress.waitUntilCompleted(oldest);
                    ringbuffer.releaseOldestAllocation();

                    block = ringbuffer.allocate(size, cmdListNumber);
                }

                block.begin = roundUpToMultiple<int64_t>(block.begin, alignment);

                // If we got here, we got space from the ringbuffer, memcpy it in and flush.
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
        };

        struct DeviceState : std::enable_shared_from_this<DeviceState>
        {
            ComPtr<IDXGIAdapter3>       adapter;
            ComPtr<ID3D12Device>        device;
            ComPtr<ID3D12CommandQueue>  graphicsQueue;
            ComPtr<ID3D12RootSignature> rootSignature;

            GrowingPool<std::shared_ptr<CommandListState>> freeGraphicsCommandLists;
            GPUProgressTracking progress;
            std::shared_ptr<UploadHeap> uploadHeap;

            ViewHeap rtvs;

            std::shared_ptr<ShaderLoader> shaderLoader;

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
                default:
                    XOR_CHECK(false, "Unknown heap type.");
                    __assume(0);
                }
            }

            void releaseDescriptor(Descriptor descriptor)
            {
                viewHeap(descriptor.type).release(descriptor);
            }

        };

        struct SwapChainState : DeviceChild
        {
            ComPtr<IDXGISwapChain3> swapChain;

            struct Backbuffer
            {
                SeqNum seqNum = InvalidSeqNum;
                TextureRTV rtv;
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

            ~ResourceState()
            {
                // Actually release the resource once every command list that could possibly have
                // referenced it has retired.

                // Queue up a no-op lambda, that holds the resource ComPtr by value.
                // When the Device has executed it, it will get destroyed, freeing the last reference.
                device().whenCompleted([resource = std::move(resource)] {});
            }
        };

        struct DescriptorViewState : DeviceChild
        {
            Descriptor descriptor;

            ~DescriptorViewState()
            {
                auto dev = device();
                dev.whenCompleted([dev, descriptor = descriptor] () mutable
                {
                    dev.S().releaseDescriptor(descriptor);
                });
            }
        };

        struct PipelineState
            : std::enable_shared_from_this<PipelineState>
            , DeviceChild
        {
            std::shared_ptr<Pipeline::Graphics> graphicsInfo;
            ComPtr<ID3D12PipelineState> pso;

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
                    dev.collectRootSignature(vs);
                    desc.VS = vs;
                }
                else
                {
                    zero(desc.VS);
                }

                if (graphicsInfo->m_ps)
                {
                    dev.collectRootSignature(ps);
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

                device().whenCompleted([pso = std::move(pso)] {});
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

            m_initializer = [&image] (CommandList &cmd, Texture &tex) 
            {
                cmd.updateTexture(tex, image.subresource(0));
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
    }

    using namespace xor::backend;

    Xor::Xor(DebugLayer debugLayer)
    {
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
        return m_adapters;
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
        for (auto &buildInfo : scanBuildInfos(shaderTlogPath, ShaderFileExtension))
            m_shaderLoader->registerBuildInfo(buildInfo);
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

        setName(device, "Device");

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (m_debug && device.As(&infoQueue) == S_OK)
        {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    TRUE);
        }

        return Device(m_adapter, std::move(device), m_shaderLoader);
    }

    Device::Device(ComPtr<IDXGIAdapter3> adapter,
                   ComPtr<ID3D12Device> dev,
                   std::shared_ptr<ShaderLoader> shaderLoader)
    {
        makeState();

        S().adapter      = std::move(adapter);
        S().shaderLoader = std::move(shaderLoader);
        S().device       = std::move(dev);

        {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            XOR_CHECK_HR(device()->CreateCommandQueue(
                &desc,
                __uuidof(ID3D12CommandQueue),
                &S().graphicsQueue));
        }

        setName(S().graphicsQueue, "Graphics Queue");

        S().uploadHeap = std::make_shared<UploadHeap>(S().device.Get());

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = MaxRTVs;
            desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask       = 0;

            S().rtvs = ViewHeap(device(), desc);
        }
    }

    SwapChain Device::createSwapChain(Window &window)
    {
        static const uint BufferCount = 2;

        auto factory = dxgiFactory();

        SwapChain swapChain;
        swapChain.makeState().setParent(*this);

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

            auto &tex = bb.rtv.m_texture;
            tex.makeState().setParent(*this);
            XOR_CHECK_HR(swapChain.S().swapChain->GetBuffer(
                i,
                __uuidof(ID3D12Resource),
                &tex.S().resource));

            tex.makeInfo() = Texture::Info(tex.S().resource.Get());

            bb.rtv.makeState().setParent(*this);
            bb.rtv.S().descriptor = S().rtvs.allocate();
            {
                D3D12_RENDER_TARGET_VIEW_DESC desc = {};
                desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                desc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice   = 0;
                desc.Texture2D.PlaneSlice = 0;
                device()->CreateRenderTargetView(
                    tex.S().resource.Get(),
                    &desc,
                    bb.rtv.S().descriptor.cpu);
            }

            swapChain.S().backbuffers.emplace_back(std::move(bb));
        }

        return swapChain;
    }

    Pipeline::Graphics::Graphics()
        : D3D12_GRAPHICS_PIPELINE_STATE_DESC {}
    {
        RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        RasterizerState.FrontCounterClockwise = TRUE;
        RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        RasterizerState.DepthClipEnable       = TRUE;

        PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        // Depth disabled by default

        multisampling(1, 0);

        SampleMask = ~0u;
        for (auto &rt : BlendState.RenderTarget)
            rt.RenderTargetWriteMask = 0xf;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC Pipeline::Graphics::desc() const
    {
        return *this;
    }

    Pipeline::Graphics &Pipeline::Graphics::vertexShader(const String & vsName)
    {
        m_vs = vsName;
        return *this;
    }

    Pipeline::Graphics &Pipeline::Graphics::pixelShader(const String & psName)
    {
        m_ps = psName;
        return *this;
    }

    Pipeline::Graphics &Pipeline::Graphics::renderTargetFormats(std::initializer_list<DXGI_FORMAT> formats)
    {
        NumRenderTargets = static_cast<uint>(formats.size());
        for (uint i = 0; i < NumRenderTargets; ++i)
            RTVFormats[i] = formats.begin()[i];
        return *this;
    }

    Pipeline::Graphics & Pipeline::Graphics::inputLayout(const info::InputLayoutInfo & ilInfo)
    {
        // Put the input layout info object behind a pointer so the element addresses
        // do not change even if the pipeline info object is copied.
        m_inputLayout = std::make_shared<info::InputLayoutInfo>(ilInfo);
        InputLayout   = m_inputLayout->desc();
        return *this;
    }

    Pipeline::Graphics &Pipeline::Graphics::multisampling(uint samples, uint quality)
    {
        SampleDesc.Count   = samples;
        SampleDesc.Quality = quality;
        return *this;
    }

    Pipeline::Graphics & Pipeline::Graphics::topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
    {
        PrimitiveTopologyType = type;
        return *this;
    }

    Pipeline::Graphics &Pipeline::Graphics::fill(D3D12_FILL_MODE fillMode)
    {
        RasterizerState.FillMode = fillMode;
        return *this;
    }

    Pipeline::Graphics &Pipeline::Graphics::cull(D3D12_CULL_MODE cullMode)
    {
        RasterizerState.CullMode = cullMode;
        return *this;
    }

    Pipeline Device::createGraphicsPipeline(const Pipeline::Graphics &info)
    {

        Pipeline pipeline;
        pipeline.makeState();
        pipeline.S().device       = weak();
        pipeline.S().graphicsInfo = std::make_shared<Pipeline::Graphics>(info);
        pipeline.S().reload();
        return pipeline;
    }

    ID3D12Device *Device::device()
    {
        return S().device.Get();
    }

    std::shared_ptr<CommandListState> Device::createCommandList()
    {
        auto &dev = S().device;

        std::shared_ptr<CommandListState> cmd =
            std::make_shared<CommandListState>();

        cmd->device = weak();

        XOR_CHECK_HR(dev->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),
            &cmd->allocator));

        XOR_CHECK_HR(dev->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            cmd->allocator.Get(),
            nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            &cmd->cmd));

        XOR_CHECK_HR(dev->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE, 
            __uuidof(ID3D12Fence),
            &cmd->timesCompleted));

        cmd->completedEvent = CreateEventExA(nullptr, nullptr, 0, 
                                             EVENT_ALL_ACCESS);
        XOR_CHECK(!!cmd->completedEvent, "Failed to create completion event.");

        return cmd;
    }

    void Device::collectRootSignature(const D3D12_SHADER_BYTECODE &shader)
    { 
        if (S().rootSignature || shader.BytecodeLength == 0)
            return;

        XOR_CHECK_HR(device()->CreateRootSignature(
            0,
            shader.pShaderBytecode,
            shader.BytecodeLength,
            __uuidof(ID3D12RootSignature),
            &S().rootSignature));
    }

    CommandList Device::initializerCommandList()
    {
        return graphicsCommandList();
    }

    Buffer Device::createBuffer(const Buffer::Info & info)
    {
        Buffer buffer;
        buffer.makeInfo() = info;
        buffer.makeState();
        buffer.S().device = weak();

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
            D3D12_RESOURCE_STATE_COMMON,
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

    Texture Device::createTexture(const Texture::Info & info)
    {
        Texture texture;
        texture.makeInfo() = info;
        texture.makeState();
        texture.S().device = weak();

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type                 = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask     = 0;
        heap.VisibleNodeMask      = 0;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width              = info.size.x;
        desc.Height             = info.size.y;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = info.format;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        XOR_CHECK_HR(device()->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
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
        TextureSRV srv;
        srv.m_texture = texture;
        return srv;
    }

    TextureSRV Device::createTextureSRV(const Texture::Info & textureInfo, const TextureSRV::Info & viewInfo)
    {
        return createTextureSRV(createTexture(textureInfo), viewInfo);
    }

    CommandList Device::graphicsCommandList()
    {
        CommandList cmd;
        cmd.m_state = S().freeGraphicsCommandLists.allocate([this]
        {
            return this->createCommandList();
        });

        cmd.reset();
        cmd.S().cmd->SetGraphicsRootSignature(S().rootSignature.Get());
        cmd.S().cmd->SetComputeRootSignature(S().rootSignature.Get());

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
        // The backbuffer is assumed to depend on all command lists
        // that have been executed, but not on those which have
        // been started but not executed. Otherwise, deadlock could result.
        backbuffer.seqNum = S().progress.newestExecuted;
        swapChain.S().swapChain->Present(vsync ? 1 : 0, 0);
        S().shaderLoader->scanChangedSources();
        S().progress.retireCommandLists();
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
            S().closed = true;
        }
    }

    void CommandList::reset()
    {
        if (S().closed)
        {
            XOR_CHECK_HR(cmd()->Reset(S().allocator.Get(), nullptr));
            S().closed = false;
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

    CommandList::~CommandList()
    {
        if (m_state)
        {
            auto &freeCmds = Device::parent(S().device).S().freeGraphicsCommandLists;
            freeCmds.release(std::move(m_state));
        }
    }

    SeqNum CommandList::number() const
    {
        return S().seqNum;
    }

    void CommandList::bind(Pipeline &pipeline)
    {
        cmd()->SetPipelineState(pipeline.S().pso.Get());
    }

    void CommandList::clearRTV(TextureRTV &rtv, float4 color)
    {
        cmd()->ClearRenderTargetView(rtv.S().descriptor.cpu,
                                     color.data(),
                                     0,
                                     nullptr);
    }

    void CommandList::setRenderTargets()
    {
        cmd()->OMSetRenderTargets(0,
                                  nullptr,
                                  false,
                                  nullptr);
    }

    void CommandList::setRenderTargets(TextureRTV &rtv)
    {
        cmd()->OMSetRenderTargets(1,
                                  &rtv.S().descriptor.cpu,
                                  FALSE,
                                  nullptr);

        D3D12_VIEWPORT viewport = {};
        D3D12_RECT scissor = {};

        auto tex = rtv.texture();

        viewport.Width    = static_cast<float>(tex->size.x);
        viewport.Height   = static_cast<float>(tex->size.y);
        viewport.MinDepth = D3D12_MIN_DEPTH;
        viewport.MaxDepth = D3D12_MAX_DEPTH;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;

        scissor.left   = 0;
        scissor.top    = 0;
        scissor.right  = static_cast<LONG>(tex->size.x);
        scissor.bottom = static_cast<LONG>(tex->size.y);

        cmd()->RSSetViewports(1, &viewport);
        cmd()->RSSetScissorRects(1, &scissor);
    }

    void CommandList::setVBV(const BufferVBV & vbv)
    {
        cmd()->IASetVertexBuffers(0, 1, &vbv.m_vbv);
    }

    void CommandList::setIBV(const BufferIBV & ibv)
    {
        cmd()->IASetIndexBuffer(&ibv.m_ibv);
    }

    void CommandList::setTopology(D3D_PRIMITIVE_TOPOLOGY topology)
    {
        cmd()->IASetPrimitiveTopology(topology);
    }

    void CommandList::barrier(Span<const Barrier> barriers)
    {
        cmd()->ResourceBarrier(static_cast<UINT>(barriers.size()),
                               barriers.begin());
    }

    void CommandList::draw(uint vertices, uint startVertex)
    {
        cmd()->DrawInstanced(vertices, 1, startVertex, 0);
    }

    void CommandList::drawIndexed(uint indices, uint startIndex)
    {
        cmd()->DrawIndexedInstanced(indices, 1, startIndex, 0, 0); 
    }

    void CommandList::updateBuffer(Buffer & buffer,
                                   Span<const uint8_t> data,
                                   size_t offset)
    {
        auto &s          = S();
        auto &dev        = Device::parent(S().device).S();
        auto &uploadHeap = *dev.uploadHeap;

        auto block       = uploadHeap.uploadBytes(dev.progress, data, number());

        S().cmd->CopyBufferRegion(
            buffer.get(),
            offset,
            uploadHeap.heap.Get(),
            static_cast<UINT64>(block.begin),
            block.size());
    }

    void CommandList::updateTexture(Texture & texture, ImageData data, uint2 pos, Subresource sr)
    {
        auto &s          = S();
        auto &dev        = Device::parent(S().device).S();
        auto &uploadHeap = *dev.uploadHeap;

        auto block       = uploadHeap.uploadBytes(dev.progress, data.data, number(),
                                                  D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource                   = texture.get();
        dst.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex            = sr.index(1);

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource                          = uploadHeap.heap.Get();
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = static_cast<UINT64>(block.begin);
        src.PlacedFootprint.Footprint.Format   = data.format;
        src.PlacedFootprint.Footprint.Width    = data.size.x;
        src.PlacedFootprint.Footprint.Height   = data.size.y;
        src.PlacedFootprint.Footprint.Depth    = 1;
        src.PlacedFootprint.Footprint.RowPitch = data.pitch;

        S().cmd->CopyTextureRegion(
            &dst,
            pos.x, pos.y, 0,
            &src,
            nullptr);
    }

    uint SwapChain::currentIndex()
    {
        // Block until the current backbuffer has finished rendering.
        for (;;)
        {
            uint index = S().swapChain->GetCurrentBackBufferIndex();

            auto device = Device::parent(S().device);

            auto &cur = S().backbuffers[index];
            if (cur.seqNum < 0 || device.hasCompleted(cur.seqNum))
                return index;

            // If we got here, the backbuffer was presented, but hasn't finished yet.
            device.waitUntilCompleted(cur.seqNum);
        }
    }

    TextureRTV SwapChain::backbuffer()
    {
        return S().backbuffers[currentIndex()].rtv;
    }

    Texture TextureView::texture()
    {
        return m_texture;
    }

    Barrier transition(Resource &resource,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after,
                       uint subresource)
    {
        Barrier barrier;
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = resource.get();
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = subresource;
        return barrier;
    }

    ID3D12Resource *Resource::get()
    {
        return m_state ? S().resource.Get() : nullptr;
    }
}
