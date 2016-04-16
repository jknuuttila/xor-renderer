#include "Core/Core.hpp"
#include <d3d12.h>
#include <dxgi1_5.h>

using namespace xor;

class HelloD3D12 : public Window
{
    ComPtr<ID3D12Device> m_device;
    ComPtr<IDXGISwapChain3> m_swapChain;
public:
    HelloD3D12()
        : Window {"Hello, D3D12!", 1600, 900}
    {
        ComPtr<IDXGIFactory2> factory;
        XOR_CHECK_HR(CreateDXGIFactory1(
            __uuidof(IDXGIFactory2),
            &factory));
        XOR_CHECK_HR(D3D12CreateDevice(
            nullptr,
            D3D_FEATURE_LEVEL_12_0,
            __uuidof(ID3D12Device),
            &m_device));

        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            ComPtr<IDXGISwapChain1> swapChain;
            XOR_CHECK_HR(factory->CreateSwapChainForHwnd(
                m_device.Get(),
                hWnd(),
                nullptr,
                nullptr,
                nullptr,
                nullptr));
        }

    }

    void mainLoop() override
    {
    }
};

int main(int argc, const char *argv[])
{
    HelloD3D12 hello;
    return hello.run();
}

