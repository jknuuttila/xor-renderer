#include "Xor.hpp"

namespace xor
{
    namespace backend
    {
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

        struct CommandListState
        {
            Device::Weak        device;
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

            SequenceTracker commandListSequence;
            std::vector<CommandList> executedCommandLists;
            SeqNum newestExecuted = 0;

            ViewHeap rtvs;

            std::priority_queue<CompletionCallback> completionCallbacks;

            ~DeviceState()
            {
                // Drain the GPU of all work before destroying objects.
                for (;;)
                {
                    retireCommandLists();

                    if (executedCommandLists.empty())
                        break;

                    executedCommandLists.front().waitUntilCompleted();
                }
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
                    commandListSequence.complete(cmd->seqNum);
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
                    }
                    completionCallbacks.pop();
                }
            }

        };

        struct SwapChainState
        {
            Device::Weak            device;
            ComPtr<IDXGISwapChain3> swapChain;

            struct Backbuffer
            {
                SeqNum seqNum = InvalidSeqNum;
                RTV rtv;
            };
            std::vector<Backbuffer> backbuffers;

            ~SwapChainState()
            {
                Device::parent(device).waitUntilDrained();
            }
        };

        struct ResourceState
        {
            Device::Weak device;
            ComPtr<ID3D12Resource> resource;

            ~ResourceState()
            {
                // Actually release the resource once every command list that could possibly have
                // referenced it has retired.

                // Queue up a no-op lambda, that holds the resource ComPtr by value.
                // When the Device has executed it, it will get destroyed, freeing the last reference.
                Device::parent(device).whenCompleted([resource = std::move(resource)] {});
            }
        };

        struct ViewState
        {
            Device::Weak device;
            Descriptor descriptor;

            ~ViewState()
            {
                Device::parent(device).whenCompleted([device = device, descriptor = descriptor] () mutable
                {
                    Device::parent(device)->releaseDescriptor(descriptor);
                });
            }
        };

        struct PipelineState
        {
            Device::Weak device;
            ComPtr<ID3D12PipelineState> pso;

            ~PipelineState()
            {
                Device::parent(device).whenCompleted([pso = std::move(pso)] {});
            }
        };
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
                        a.m_description = String(desc.Description);
                        a.m_debug = debugLayer == DebugLayer::Enabled;
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

    span<Adapter> Xor::adapters()
    {
        return m_adapters;
    }

    Adapter & Xor::defaultAdapter()
    {
        XOR_CHECK(!m_adapters.empty(), "No adapters detected!");
        return m_adapters[0];
    }

    Device Adapter::createDevice(D3D_FEATURE_LEVEL minimumFeatureLevel)
    {
        Device device = Device(m_adapter, minimumFeatureLevel);

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (false && m_debug && device->device.As(&infoQueue) == S_OK)
        {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    TRUE);
        }

        return device;
    }

    Device::Device(ComPtr<IDXGIAdapter3> adapter, D3D_FEATURE_LEVEL minimumFeatureLevel)
    {
        makeState();

        m_state->adapter = std::move(adapter);

        XOR_CHECK_HR(D3D12CreateDevice(
            m_state->adapter.Get(),
            minimumFeatureLevel,
            __uuidof(ID3D12Device),
            &m_state->device));

        setName(m_state->device, "Device");

        {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            XOR_CHECK_HR(device()->CreateCommandQueue(
                &desc,
                __uuidof(ID3D12CommandQueue),
                &m_state->graphicsQueue));
        }

        setName(m_state->graphicsQueue, "Graphics Queue");

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = MaxRTVs;
            desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask       = 0;

            m_state->rtvs = ViewHeap(device(), desc);
        }
    }

    SwapChain Device::createSwapChain(Window &window)
    {
        static const uint BufferCount = 2;

        auto factory = dxgiFactory();

        SwapChain swapChain;
        swapChain.makeState();

        swapChain->device = weak();

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
                m_state->graphicsQueue.Get(),
                window.hWnd(),
                &desc,
                nullptr,
                nullptr,
                &swapChain1));

            XOR_CHECK_HR(swapChain1.As(&swapChain->swapChain));
        }

        for (uint i = 0; i < BufferCount; ++i)
        {
            SwapChainState::Backbuffer bb;

            auto &tex = bb.rtv.m_texture;
            tex.makeState();
            tex->device = weak();
            XOR_CHECK_HR(swapChain->swapChain->GetBuffer(
                i,
                __uuidof(ID3D12Resource),
                &tex->resource));

            bb.rtv.makeState();
            bb.rtv->device = weak();
            bb.rtv->descriptor = m_state->rtvs.allocate();
            {
                D3D12_RENDER_TARGET_VIEW_DESC desc = {};
                desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                desc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice   = 0;
                desc.Texture2D.PlaneSlice = 0;
                device()->CreateRenderTargetView(
                    tex->resource.Get(),
                    &desc,
                    bb.rtv->descriptor.cpu);
            }

            swapChain->backbuffers.emplace_back(std::move(bb));
        }

        return swapChain;
    }

    struct ShaderBinary : public File, public D3D12_SHADER_BYTECODE
    {
        ShaderBinary()
        {
            pShaderBytecode = nullptr;
            BytecodeLength  = 0;
        }

        ShaderBinary(const String &filename)
            : File(filename)
        {
            XOR_CHECK_HR(hr());
            pShaderBytecode = data();
            BytecodeLength  = size();
        }
    };

    Pipeline::Graphics::Graphics()
        : D3D12_GRAPHICS_PIPELINE_STATE_DESC {}
    {
        RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
        RasterizerState.FrontCounterClockwise = TRUE;
        RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        NumRenderTargets      = 1;
        RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        SampleDesc.Count      = 1;
        SampleDesc.Quality    = 0;

        SampleMask = ~0;
        BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf;
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

    Pipeline Device::createGraphicsPipeline(const Pipeline::Graphics &info)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = info;

        ShaderBinary vs;
        ShaderBinary ps;

        static const char ShaderFileExtension[] = ".cso";
        if (info.m_vs) vs = ShaderBinary(info.m_vs + ShaderFileExtension);
        if (info.m_ps) ps = ShaderBinary(info.m_ps + ShaderFileExtension);

        collectRootSignature(vs);
        collectRootSignature(ps);

        desc.VS = vs;
        desc.PS = ps;

        Pipeline pipeline;
        pipeline.makeState();
        pipeline->device = weak();
        XOR_CHECK_HR(device()->CreateGraphicsPipelineState(
            &desc,
            __uuidof(ID3D12PipelineState),
            &pipeline->pso));

        return pipeline;
    }

    Device Device::parent(Device::Weak& parentDevice)
    {
        Device parent;
        if (auto devState = parentDevice.lock())
        {
            parent.m_state = std::move(devState);
        }
        return parent;
    }

    ID3D12Device *Device::device()
    {
        return m_state->device.Get();
    }

    std::shared_ptr<CommandListState> Device::createCommandList()
    {
        auto &dev = m_state->device;

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
        if (m_state->rootSignature || shader.BytecodeLength == 0)
            return;

        XOR_CHECK_HR(device()->CreateRootSignature(
            0,
            shader.pShaderBytecode,
            shader.BytecodeLength,
            __uuidof(ID3D12RootSignature),
            &m_state->rootSignature));
    }

    CommandList Device::graphicsCommandList()
    {
        CommandList cmd;
        cmd.m_state = m_state->freeGraphicsCommandLists.allocate([this]
        {
            return this->createCommandList();
        });

        cmd.reset();
        cmd->cmd->SetGraphicsRootSignature(m_state->rootSignature.Get());
        cmd->cmd->SetComputeRootSignature(m_state->rootSignature.Get());

        ++cmd->timesStarted;
        cmd->seqNum = m_state->commandListSequence.start();

        return cmd;
    }

    void Device::execute(CommandList &cmd)
    {
        cmd.close();

        ID3D12CommandList *cmds[] = { cmd->cmd.Get() };
        m_state->graphicsQueue->ExecuteCommandLists(1, cmds);
        m_state->graphicsQueue->Signal(cmd->timesCompleted.Get(), cmd->timesStarted);

        m_state->newestExecuted = std::max(m_state->newestExecuted, cmd->seqNum);
        m_state->executedCommandLists.emplace_back(std::move(cmd));
    }

    void Device::present(SwapChain & swapChain, bool vsync)
    {
        auto &backbuffer = swapChain->backbuffers[swapChain.currentIndex()];
        // The backbuffer is assumed to depend on all command lists
        // that have been executed, but not on those which have
        // been started but not executed. Otherwise, deadlock could result.
        backbuffer.seqNum = m_state->newestExecuted;
        swapChain->swapChain->Present(vsync ? 1 : 0, 0);
        m_state->retireCommandLists();
    }

    SeqNum Device::now()
    {
        return m_state->commandListSequence.newestStarted();
    }

    void Device::whenCompleted(std::function<void()> f)
    {
        whenCompleted(std::move(f), now());
    }

    void Device::whenCompleted(std::function<void()> f, SeqNum seqNum)
    {
        if (hasCompleted(seqNum))
            f();
        else
            m_state->completionCallbacks.emplace(seqNum, std::move(f));
    }

    bool Device::hasCompleted(SeqNum seqNum)
    {
        m_state->retireCommandLists();
        return m_state->commandListSequence.hasCompleted(seqNum);
    }

    void Device::waitUntilCompleted(SeqNum seqNum)
    {
        while (!hasCompleted(seqNum))
        {
            auto &executed = m_state->executedCommandLists;
            XOR_CHECK(!executed.empty(), "Nothing to wait for, deadlock!");
            executed.front().waitUntilCompleted();
        }
    }

    void Device::waitUntilDrained()
    {
        for (;;)
        {
            auto newest = m_state->commandListSequence.newestStarted();
            if (hasCompleted(newest))
                return;
            else
                waitUntilCompleted(newest);
        }
    }

    ID3D12GraphicsCommandList *CommandList::cmd()
    {
        return m_state->cmd.Get();
    }

    void CommandList::close()
    {
        if (!m_state->closed)
        {
            XOR_CHECK_HR(cmd()->Close());
            m_state->closed = true;
        }
    }

    void CommandList::reset()
    {
        if (m_state->closed)
        {
            XOR_CHECK_HR(cmd()->Reset(m_state->allocator.Get(), nullptr));
            m_state->closed = false;
        }
    }

    bool CommandList::hasCompleted()
    {
        auto completed = m_state->timesCompleted->GetCompletedValue();

        XOR_ASSERT(completed <= m_state->timesStarted,
                   "Command list completion count out of sync.");

        return completed == m_state->timesStarted;
    }

    void CommandList::waitUntilCompleted(DWORD timeout)
    {
        while (!hasCompleted())
        {
            XOR_CHECK_HR(m_state->timesCompleted->SetEventOnCompletion(
                m_state->timesStarted,
                m_state->completedEvent.get()));
            WaitForSingleObject(m_state->completedEvent.get(), timeout);
        }
    }

    CommandList::~CommandList()
    {
        if (m_state)
        {
            auto &freeCmds = Device::parent(m_state->device)->freeGraphicsCommandLists;
            freeCmds.release(std::move(m_state));
        }
    }

    void CommandList::bind(Pipeline &pipeline)
    {
        cmd()->SetPipelineState(pipeline->pso.Get());
    }

    void CommandList::clearRTV(RTV &rtv, float4 color)
    {
        cmd()->ClearRenderTargetView(rtv->descriptor.cpu,
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

    void CommandList::setRenderTargets(RTV &rtv)
    {
        cmd()->OMSetRenderTargets(1,
                                  &rtv->descriptor.cpu,
                                  FALSE,
                                  nullptr);

        D3D12_VIEWPORT viewport = {};
        D3D12_RECT scissor = {};

        auto texDesc      = rtv.texture().desc();

        viewport.Width    = static_cast<float>(texDesc.Width);
        viewport.Height   = static_cast<float>(texDesc.Height);
        viewport.MinDepth = D3D12_MIN_DEPTH;
        viewport.MaxDepth = D3D12_MAX_DEPTH;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;

        scissor.left   = 0;
        scissor.top    = 0;
        scissor.right  = static_cast<LONG>(texDesc.Width);
        scissor.bottom = static_cast<LONG>(texDesc.Height);

        cmd()->RSSetViewports(1, &viewport);
        cmd()->RSSetScissorRects(1, &scissor);
    }

    void CommandList::barrier(std::initializer_list<Barrier> barriers)
    {
        cmd()->ResourceBarrier(static_cast<UINT>(barriers.size()),
                               barriers.begin());
    }

    void CommandList::draw(uint vertices, uint startVertex)
    {
        cmd()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd()->DrawInstanced(vertices, 1, startVertex, 0);
    }

    uint SwapChain::currentIndex()
    {
        // Block until the current backbuffer has finished rendering.
        for (;;)
        {
            uint index = m_state->swapChain->GetCurrentBackBufferIndex();

            auto device = Device::parent(m_state->device);

            auto &cur = m_state->backbuffers[index];
            if (cur.seqNum < 0 || device.hasCompleted(cur.seqNum))
                return index;

            // If we got here, the backbuffer was presented, but hasn't finished yet.
            device.waitUntilCompleted(cur.seqNum);
        }
    }

    RTV SwapChain::backbuffer()
    {
        return m_state->backbuffers[currentIndex()].rtv;
    }

    Texture RTV::texture()
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

    D3D12_RESOURCE_DESC Resource::desc() const
    {
        return m_state->resource->GetDesc();
    }

    ID3D12Resource *Resource::get()
    {
        return m_state ? m_state->resource.Get() : nullptr;
    }
}
