#pragma once

#include "Core/Core.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>

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

    class SwapChain
    {
        friend class Device;
        ComPtr<IDXGISwapChain3> m_swapChain;
    public:
    };

    class Device
    {
        friend class Adapter;

        ComPtr<IDXGIAdapter3>      m_adapter;
        ComPtr<ID3D12Device>       m_device;
        ComPtr<ID3D12CommandQueue> m_graphicsQueue;

        Device(ComPtr<IDXGIAdapter3> adapter, D3D_FEATURE_LEVEL minimumFeatureLevel);
    public:
        Device() {}

        SwapChain createSwapChain(Window &window);
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

