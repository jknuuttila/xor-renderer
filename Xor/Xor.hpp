#pragma once

#include "Core/Core.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>
#include <initializer_list>
#include <queue>

namespace xor
{
    class Device;
    class Adapter
    {
        friend class Xor;

        ComPtr<IDXGIAdapter3> m_adapter;
        String                m_description;
    public:
        Device createDevice(D3D_FEATURE_LEVEL minimumFeatureLevel = D3D_FEATURE_LEVEL_12_0);
    };

    class Resource
    {
    public:
    };

    class Texture : public Resource
    {
    public:
    };

    class RTV
    {
    public:
        Texture texture();
    };

    class Barrier
    {
    public:
    };

    Barrier transition(Resource &resource,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after,
                       uint subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    class SwapChain
    {
        friend class Device;
        ComPtr<IDXGISwapChain3> m_swapChain;
    public:
        RTV backbuffer();
    };

    class CommandList
    {
        friend class Device;

        ComPtr<ID3D12CommandAllocator>    m_allocator;
        ComPtr<ID3D12GraphicsCommandList> m_cmd;

        uint64_t            m_timesStarted = 0;
        ComPtr<ID3D12Fence> m_timesCompleted;
        Handle              m_completedEvent;

        uint64_t m_seqNum = 0;

        CommandList(Device &device);
        bool hasCompleted();
        void waitUntilCompleted(DWORD timeout = INFINITE);

    public:
        CommandList() {}
        ~CommandList();

        CommandList(CommandList &&) = default;
        CommandList& operator=(CommandList &&) = default;

        CommandList(const CommandList &) = delete;
        CommandList& operator=(const CommandList &) = delete;

        void clearRTV(RTV &rtv, float4 color = 0);
        void barrier(std::initializer_list<Barrier> barriers);
    };

    class Device
    {
        friend class Adapter;
        friend class CommandList;

        ComPtr<IDXGIAdapter3>      m_adapter;
        ComPtr<ID3D12Device>       m_device;
        ComPtr<ID3D12CommandQueue> m_graphicsQueue;

        GrowingPool<CommandList>   m_freeGraphicsCommandLists;

        SequenceTracker m_commandListSequence;
        std::vector<CommandList> m_executedCommandLists;
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
        std::priority_queue<CompletionCallback> m_completionCallbacks;

        void retireCommandLists();

        Device(ComPtr<IDXGIAdapter3> adapter, D3D_FEATURE_LEVEL minimumFeatureLevel);
    public:
        Device() {}

        SwapChain createSwapChain(Window &window);

        CommandList graphicsCommandList();

        void execute(CommandList &cmd);
        void present(SwapChain &swapChain, bool vsync = true);
    };

    // Global initialization and deinitialization of the Xor renderer.
    class Xor
    {
        std::vector<Adapter>  m_adapters;
    public:
        enum class DebugLayer
        {
            Enabled,
            Disabled,
        };

        Xor(DebugLayer debugLayer = DebugLayer::Enabled);
        ~Xor();

        span<Adapter> adapters();
        Adapter &defaultAdapter();
    };
}

