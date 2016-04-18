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
        struct ResourceState;
        struct ViewState;

        template <typename T>
        struct SharedState
        {
            std::shared_ptr<T> state;
            void makeState() { state = std::make_shared<T>(); }
            T *operator->() { return state.get(); }
        };
    }

    class SwapChain;
    class CommandList;
    class Device : private state::SharedState<state::DeviceState>
    {
        friend class Adapter;
        friend class CommandList;

        void retireCommandLists();

        Device(ComPtr<IDXGIAdapter3> adapter, D3D_FEATURE_LEVEL minimumFeatureLevel);
    public:
        Device() {}

        SwapChain createSwapChain(Window &window);

        CommandList graphicsCommandList();

        void execute(CommandList &cmd);
        void present(SwapChain &swapChain, bool vsync = true);

        // TODO: Change seqNums to int64_t in public APIs
        uint64_t now();
        void whenCompleted(std::function<void()> f);
        void whenCompleted(std::function<void()> f, uint64_t seqNum);
        bool hasCompleted(uint64_t seqNum);
        void waitUntilCompleted(uint64_t seqNum);
    };

    class Resource : private state::SharedState<state::ResourceState>
    {
        friend class Device;
        friend class CommandList;
    public:
    };

    class View : private state::SharedState<state::ViewState>
    {
        friend class Device;
        friend class CommandList;
    public:
    };

    class Texture : public Resource
    {
    public:
    };

    class RTV : public View
    {
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

    class SwapChain
    {
        friend class Device;
        Device m_device;
        ComPtr<IDXGISwapChain3> m_swapChain;

        struct Backbuffer
        {
            int64_t seqNum = -1;
            RTV rtv;
        };
        std::vector<Backbuffer> m_backbuffers;

        Backbuffer &current();

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

