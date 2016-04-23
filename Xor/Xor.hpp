#pragma once

#include "Core/Core.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>
#include <initializer_list>
#include <queue>
#include <memory>

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

    namespace state
    {
        struct DeviceState;
        struct CommandListState;
        struct SwapChainState;
        struct ResourceState;
        struct ViewState;

        template <typename T>
        struct SharedState
        {
            std::shared_ptr<T> m_state;
            void makeState() { m_state = std::make_shared<T>(); }
            T *operator->() { return m_state.get(); }
        };
    }

    class SwapChain;
    class CommandList;
    class Device : private state::SharedState<state::DeviceState>
    {
        friend class Adapter;
        friend class CommandList;

        ID3D12Device *device();
        std::shared_ptr<state::CommandListState> createCommandList();
        void retireCommandLists();

        Device(ComPtr<IDXGIAdapter3> adapter, D3D_FEATURE_LEVEL minimumFeatureLevel);
    public:
        Device() {}

        SwapChain createSwapChain(Window &window);

        CommandList graphicsCommandList();

        void execute(CommandList &cmd);
        void present(SwapChain &swapChain, bool vsync = true);

        SeqNum now();
        void whenCompleted(std::function<void()> f);
        void whenCompleted(std::function<void()> f, SeqNum seqNum);
        bool hasCompleted(SeqNum seqNum);
        void waitUntilCompleted(SeqNum seqNum);
    };

    class Resource : private state::SharedState<state::ResourceState>
    {
        friend class Device;
        friend class CommandList;
    public:
        Resource();
        ~Resource();
    };

    class View : private state::SharedState<state::ViewState>
    {
        friend class Device;
        friend class CommandList;
    public:
        View();
        ~View();
    };

    class Texture : public Resource
    {
        friend class Device;
        friend class CommandList;
    public:
    };

    class RTV : public View
    {
        friend class Device;
        friend class CommandList;
        Texture m_texture;
    public:
        Texture texture();
    };

    class Barrier : public D3D12_RESOURCE_BARRIER
    {
    public:
    };

    Barrier transition(Resource &resource,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after,
                       uint subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    class SwapChain : private state::SharedState<state::SwapChainState>
    {
        friend class Device;
        uint currentIndex();

    public:
        RTV backbuffer();
    };

    class CommandList : private state::SharedState<state::CommandListState>
    {
        friend class Device;

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

