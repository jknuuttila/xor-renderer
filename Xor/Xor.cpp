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

    namespace state
    {
        struct CompletionCallback
        {
            uint64_t seqNum = 0;
            std::function<void()> f;

            // Smallest seqNum goes first in a priority queue.
            bool operator<(const CompletionCallback &c) const
            {
                return seqNum > c.seqNum;
            }
        };

        struct DeviceState
        {
            ComPtr<IDXGIAdapter3>      adapter;
            ComPtr<ID3D12Device>       device;
            ComPtr<ID3D12CommandQueue> graphicsQueue;

            GrowingPool<CommandList>   freeGraphicsCommandLists;

            SequenceTracker commandListSequence;
            std::vector<CommandList> executedCommandLists;
            uint64_t newestExecuted = 0;

            std::priority_queue<CompletionCallback> completionCallbacks;
        };

        struct CommandListState
        {
            ComPtr<ID3D12CommandAllocator>    allocator;
            ComPtr<ID3D12GraphicsCommandList> cmd;

            uint64_t            timesStarted = 0;
            ComPtr<ID3D12Fence> timesCompleted;
            Handle              completedEvent;

            uint64_t seqNum = 0;
        };

        struct ResourceState
        {
            ComPtr<ID3D12Resource> resource;
            Device device;
        };

        struct ViewState
        {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = { 0 };
            Device device;
        };
    }

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
        return Device(m_adapter, minimumFeatureLevel);
    }

    Device::Device(ComPtr<IDXGIAdapter3> adapter, D3D_FEATURE_LEVEL minimumFeatureLevel)
    {
        makeState();

        state->adapter = std::move(adapter);

        XOR_CHECK_HR(D3D12CreateDevice(
            state->adapter.Get(),
            minimumFeatureLevel,
            __uuidof(ID3D12Device),
            &state->device));

        {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            XOR_CHECK_HR(state->device->CreateCommandQueue(
                &desc,
                __uuidof(ID3D12CommandQueue),
                &state->graphicsQueue));
        }
    }

    SwapChain Device::createSwapChain(Window &window)
    {
        SwapChain swapChain;

        auto factory = dxgiFactory();

        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width              = window.size().x;
            desc.Height             = window.size().y;
            desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Stereo             = false;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount        = 2;
            desc.Scaling            = DXGI_SCALING_NONE;
            desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags              = 0;

            ComPtr<IDXGISwapChain1> swapChain1;
            XOR_CHECK_HR(factory->CreateSwapChainForHwnd(
                state->graphicsQueue.Get(),
                window.hWnd(),
                &desc,
                nullptr,
                nullptr,
                &swapChain1));

            XOR_CHECK_HR(swapChain1.As(&swapChain.m_swapChain));
        }

        return swapChain;
    }

    CommandList Device::graphicsCommandList()
    {
        auto cmd = state->freeGraphicsCommandLists.allocate([this]
        {
            return CommandList(*this);
        });

        cmd->cmd->Reset(cmd->allocator.Get(), nullptr);
        ++cmd->timesStarted;
        cmd->seqNum = state->commandListSequence.start();

        return cmd;
    }

    void Device::execute(CommandList &cmd)
    {
        cmd->cmd->Close();
        ID3D12CommandList *cmds[] = { cmd->cmd.Get() };
        state->graphicsQueue->ExecuteCommandLists(1, cmds);
        state->graphicsQueue->Signal(cmd->timesCompleted.Get(), cmd->timesStarted);

        state->newestExecuted = std::max(state->newestExecuted, cmd->seqNum);
        state->executedCommandLists.emplace_back(std::move(cmd));
    }

    void Device::present(SwapChain & swapChain, bool vsync)
    {
        auto &backbuffer  = swapChain.current();
        // The backbuffer is assumed to depend on all command lists
        // that have been executed, but not on those which have
        // been started but not executed. Otherwise, deadlock could result.
        backbuffer.seqNum = state->newestExecuted;
        swapChain.m_swapChain->Present(vsync ? 1 : 0, 0);
        retireCommandLists();
    }

    bool Device::hasCompleted(uint64_t seqNum)
    {
        retireCommandLists();
        return state->commandListSequence.hasCompleted(seqNum);
    }

    void Device::waitUntilCompleted(uint64_t seqNum)
    {
        while (!hasCompleted(seqNum))
        {
            auto &executed = state->executedCommandLists;
            XOR_CHECK(!executed.empty(), "Nothing to wait for, deadlock!");
            executed.front().waitUntilCompleted();
        }
    }

    void Device::retireCommandLists()
    {
        auto &s = *state;

        unsigned completedLists = 0;

        for (auto &cmd : s.executedCommandLists)
        {
            if (cmd.hasCompleted())
                ++completedLists;
            else
                break;
        }

        for (unsigned i = 0; i < completedLists; ++i)
        {
            auto &cmd = s.executedCommandLists[i];
            s.commandListSequence.complete(cmd->seqNum);
            s.freeGraphicsCommandLists.release(std::move(cmd));
        }

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

    CommandList::CommandList(Device &device)
    {
        auto &dev = device->device;

        XOR_CHECK_HR(dev->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),
            &state->allocator));

        XOR_CHECK_HR(dev->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            state->allocator.Get(),
            nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            &state->cmd));

        // Close it when creating so we can always Reset()
        // it when reusing.
        state->cmd->Close();

        XOR_CHECK_HR(dev->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE, 
            __uuidof(ID3D12Fence),
            &state->timesCompleted));

        state->completedEvent = CreateEventExA(nullptr, nullptr, 0, 
                                               EVENT_ALL_ACCESS);
        XOR_CHECK(!!state->completedEvent, "Failed to create completion event.");
    }

    bool CommandList::hasCompleted()
    {
        auto completed = state->timesCompleted->GetCompletedValue();

        XOR_ASSERT(completed <= state->timesStarted,
                   "Command list completion count out of sync.");

        return completed == state->timesStarted;
    }

    void CommandList::waitUntilCompleted(DWORD timeout)
    {
        while (!hasCompleted())
            WaitForSingleObject(state->completedEvent.get(), timeout);
    }

    void CommandList::clearRTV(RTV &rtv, float4 color)
    {
        state->cmd->ClearRenderTargetView(rtv->descriptor,
                                          color.data(),
                                          0,
                                          nullptr);
    }

    void CommandList::barrier(std::initializer_list<Barrier> barriers)
    {
        state->cmd->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                    barriers.begin());
    }

    SwapChain::Backbuffer &SwapChain::current()
    {
        // Block until the current backbuffer has finished rendering.
        for (;;)
        {
            auto &cur = m_backbuffers[m_swapChain->GetCurrentBackBufferIndex()];

            if (cur.seqNum < 0 || m_device.hasCompleted(cur.seqNum))
                return cur;

            // If we got here, the backbuffer was presented, but hasn't finished yet.
            m_device.waitUntilCompleted(cur.seqNum);
        }
    }

    RTV SwapChain::backbuffer()
    {
        return current().rtv;
    }

    Texture RTV::texture()
    {
        return m_texture;
    }

    Barrier transition(Resource & resource,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after,
                       uint subresource)
    {
        Barrier barrier;
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = nullptr;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = subresource;
        return barrier;
    }
}
