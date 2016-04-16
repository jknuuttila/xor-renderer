#include "Xor.hpp"

namespace xor
{
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

        XOR_CHECK_HR(CreateDXGIFactory1(
            __uuidof(IDXGIFactory4),
            &m_factory));

        {
            uint i = 0;
            bool foundAdapters = true;
            while (foundAdapters)
            {
                ComPtr<IDXGIAdapter1> adapter;
                auto hr = m_factory->EnumAdapters1(i, &adapter);

                switch (hr)
                {
                case S_OK:
                    m_adapters.emplace_back();
                    {
                        auto &a = m_adapters.back();
                        XOR_CHECK_HR(adapter.As(&a.m_adapter));
                        DXGI_ADAPTER_DESC2 desc = {};
                        XOR_CHECK_HR(a.m_adapter->GetDesc2(&desc));
                        char description[sizeof(desc.Description)] = {};
                        // TODO: Replace when the custom String wrapper is implemented
                        size_t conv = 0;
                        wcstombs_s(&conv, description, desc.Description, size(desc.Description)); 
                        a.m_description = description;
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
}
