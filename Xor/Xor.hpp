#pragma once

#include "Core/Core.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>

namespace xor
{
    class Adapter
    {
        friend class Xor;
        ComPtr<IDXGIAdapter3> m_adapter;
        std::string m_description;
    public:
    };

    // Global initialization and deinitialization of the Xor renderer.
    class Xor
    {
        ComPtr<IDXGIFactory4> m_factory;
        std::vector<Adapter> m_adapters;
    public:
        enum class DebugLayer
        {
            Enabled,
            Disabled,
        };

        Xor(DebugLayer debugLayer = DebugLayer::Enabled);
        ~Xor();
    };
}

