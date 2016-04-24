#include "Xor.hpp"

namespace xor
{
    static ComPtr<IDXGIFactory4> dxgiFactory()
    {
        ComPtr<IDXGIFactory4> factory;

        XOR_CHECK_HR(CreateDXGIFactory1(
            __uuidof(IDXGIFactory4),
            &factory));

        return factory;
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
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = { 0 };
    };

    struct ViewHeap
    {
        ComPtr<ID3D12DescriptorHeap> heap;
        OffsetPool freeDescriptors;
        uint increment = 0;

        ViewHeap() = default;
        ViewHeap(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_DESC desc)
        {
            XOR_CHECK_HR(device->CreateDescriptorHeap(
                &desc,
                __uuidof(ID3D12DescriptorHeap),
                &heap));

            freeDescriptors = OffsetPool(desc.NumDescriptors);
            increment       = device->GetDescriptorHandleIncrementSize(desc.Type);
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

            return descriptor;
        }

        void release(Descriptor descriptor)
        {
            auto start = heap->GetCPUDescriptorHandleForHeapStart();
            size_t offset = descriptor.cpu.ptr - start.ptr;
            offset /= increment;
            freeDescriptors.release(static_cast<int64_t>(offset));
        }
    };

    namespace backend
    {
        struct CommandListState
        {
            Device                            device;
            ComPtr<ID3D12CommandAllocator>    allocator;
            ComPtr<ID3D12GraphicsCommandList> cmd;

            uint64_t            timesStarted = 0;
            ComPtr<ID3D12Fence> timesCompleted;
            Handle              completedEvent;

            SeqNum seqNum = 0;
            bool closed = false;
        };

        struct DeviceState
        {
            ComPtr<IDXGIAdapter3>      adapter;
            ComPtr<ID3D12Device>       device;
            ComPtr<ID3D12CommandQueue> graphicsQueue;

            GrowingPool<std::shared_ptr<CommandListState>> freeGraphicsCommandLists;

            SequenceTracker commandListSequence;
            std::vector<CommandList> executedCommandLists;
            SeqNum newestExecuted = 0;

            ViewHeap rtvs;

            std::priority_queue<CompletionCallback> completionCallbacks;
        };

        struct SwapChainState
        {
            Device                  device;
            ComPtr<IDXGISwapChain3> swapChain;

            struct Backbuffer
            {
                SeqNum seqNum = InvalidSeqNum;
                RTV rtv;
            };
            std::vector<Backbuffer> backbuffers;
        };

        struct ResourceState
        {
            Device device;
            ComPtr<ID3D12Resource> resource;
        };

        struct ViewState
        {
            Device device;
            Descriptor descriptor;
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
        if (m_debug && device->device.As(&infoQueue) == S_OK)
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

        swapChain->device = *this;

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
            tex->device = *this;
            XOR_CHECK_HR(swapChain->swapChain->GetBuffer(
                i,
                __uuidof(ID3D12Resource),
                &tex->resource));

            bb.rtv.makeState();
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

    ID3D12Device *Device::device()
    {
        return m_state->device.Get();
    }

    std::shared_ptr<CommandListState> Device::createCommandList()
    {
        auto &dev = m_state->device;

        std::shared_ptr<CommandListState> cmd =
            std::make_shared<CommandListState>();

        cmd->device = *this;

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

    CommandList Device::graphicsCommandList()
    {
        CommandList cmd;
        cmd.m_state = m_state->freeGraphicsCommandLists.allocate([this]
        {
            return this->createCommandList();
        });

        cmd.reset();

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
        retireCommandLists();
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
        retireCommandLists();
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

    void Device::retireCommandLists()
    {
        auto &s = *m_state;

        uint completedLists = 0;

        for (auto &cmd : s.executedCommandLists)
        {
            if (cmd.hasCompleted())
                ++completedLists;
            else
                break;
        }

        for (uint i = 0; i < completedLists; ++i)
        {
            auto &cmd = s.executedCommandLists[i];
            s.commandListSequence.complete(cmd->seqNum);
        }

        // This will also return the command list states to the pool
        s.executedCommandLists.erase(s.executedCommandLists.begin(),
                                     s.executedCommandLists.begin() + completedLists);

        while (!s.completionCallbacks.empty())
        {
            auto &top = s.completionCallbacks.top();
            if (s.commandListSequence.hasCompleted(top.seqNum))
            {
                top.f();
            }
            s.completionCallbacks.pop();
        }
    }

    void CommandList::close()
    {
        if (!m_state->closed)
        {
            XOR_CHECK_HR(m_state->cmd->Close());
            m_state->closed = true;
        }
    }

    void CommandList::reset()
    {
        if (m_state->closed)
        {
            XOR_CHECK_HR(m_state->cmd->Reset(m_state->allocator.Get(), nullptr));
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
            auto &freeCmds = m_state->device->freeGraphicsCommandLists;
            freeCmds.release(std::move(m_state));
        }
    }

    void CommandList::clearRTV(RTV &rtv, float4 color)
    {
        m_state->cmd->ClearRenderTargetView(rtv->descriptor.cpu,
                                            color.data(),
                                            0,
                                            nullptr);
    }

    void CommandList::barrier(std::initializer_list<Barrier> barriers)
    {
        m_state->cmd->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                      barriers.begin());
    }

    uint SwapChain::currentIndex()
    {
        // Block until the current backbuffer has finished rendering.
        for (;;)
        {
            uint index = m_state->swapChain->GetCurrentBackBufferIndex();

            auto &cur = m_state->backbuffers[index];
            if (cur.seqNum < 0 || m_state->device.hasCompleted(cur.seqNum))
                return index;

            // If we got here, the backbuffer was presented, but hasn't finished yet.
            m_state->device.waitUntilCompleted(cur.seqNum);
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

    Resource::Resource()
    {
    }

    Resource::~Resource()
    {
    }

    ID3D12Resource *Resource::get()
    {
        return m_state ? m_state->resource.Get() : nullptr;
    }

    View::View()
    {
    }

    View::~View()
    {
    }
}
