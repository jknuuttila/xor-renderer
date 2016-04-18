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
        : m_adapter(std::move(adapter))
    {
        XOR_CHECK_HR(D3D12CreateDevice(
            m_adapter.Get(),
            minimumFeatureLevel,
            __uuidof(ID3D12Device),
            &m_device));

        {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            XOR_CHECK_HR(m_device->CreateCommandQueue(
                &desc,
                __uuidof(ID3D12CommandQueue),
                &m_graphicsQueue));
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
                m_graphicsQueue.Get(),
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
        auto cmd = m_freeGraphicsCommandLists.allocate([this]
        {
            return CommandList(*this);
        });

        cmd.m_cmd->Reset(cmd.m_allocator.Get(), nullptr);
        ++cmd.m_timesStarted;
        cmd.m_seqNum = m_commandListSequence.start();

        return cmd;
    }

    void Device::execute(CommandList &cmd)
    {
        ID3D12CommandList *cmds[] = { cmd.m_cmd.Get() };
        m_graphicsQueue->ExecuteCommandLists(1, cmds);
        m_graphicsQueue->Signal(cmd.m_timesCompleted.Get(), cmd.m_timesStarted);

        m_executedCommandLists.emplace_back(std::move(cmd));
    }

    void Device::present(SwapChain & swapChain, bool vsync)
    {
        swapChain.m_swapChain->Present(vsync ? 1 : 0, 0);
        retireCommandLists();
    }

    void Device::retireCommandLists()
    {
        unsigned completedLists = 0;

        for (auto &cmd : m_executedCommandLists)
        {
            if (cmd.hasCompleted())
                ++completedLists;
            else
                break;
        }

        for (unsigned i = 0; i < completedLists; ++i)
        {
            auto &cmd = m_executedCommandLists[i];
            m_commandListSequence.complete(cmd.m_seqNum);
            m_freeGraphicsCommandLists.release(std::move(cmd));
        }

        m_executedCommandLists.erase(m_executedCommandLists.begin(),
                                     m_executedCommandLists.begin() + completedLists);

        while (!m_completionCallbacks.empty())
        {
            auto &top = m_completionCallbacks.top();
            if (m_commandListSequence.hasCompleted(top.seqNum))
            {
                top.f();
            }
            m_completionCallbacks.pop();
        }
    }

    CommandList::CommandList(Device &device)
    {
        auto &dev = device.m_device;

        XOR_CHECK_HR(dev->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),
            &m_allocator));

        XOR_CHECK_HR(dev->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_allocator.Get(),
            nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            &m_cmd));

        // Close it when creating so we can always Reset()
        // it when reusing.
        m_cmd->Close();

        XOR_CHECK_HR(dev->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE, 
            __uuidof(ID3D12Fence),
            &m_timesCompleted));

        m_completedEvent = CreateEventExA(nullptr, nullptr, 0, 
                                          EVENT_ALL_ACCESS);
        XOR_CHECK(!!m_completedEvent, "Failed to create completion event.");
    }

    bool CommandList::hasCompleted()
    {
        auto completed = m_timesCompleted->GetCompletedValue();

        XOR_ASSERT(completed <= m_timesStarted,
                   "Command list completion count out of sync.");

        return completed == m_timesStarted;
    }

    void CommandList::waitUntilCompleted(DWORD timeout)
    {
        while (!hasCompleted())
            WaitForSingleObject(m_completedEvent.get(), timeout);
    }
}
