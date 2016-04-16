#include "Core/Core.hpp"
#include "Core/Math.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>

using namespace xor;

class HelloD3D12 : public Window
{
    static const uint BufferCount = 3;
    ComPtr<ID3D12Device>       m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<IDXGISwapChain3>    m_swapChain;

    struct Frame
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> cmd;
        ComPtr<ID3D12Resource> rt;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        uint64_t number;
    };
    ComPtr<ID3D12Fence> m_frameFence;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    std::vector<Frame> m_frames;

    uint64_t m_frameNumber = 0;

public:
    HelloD3D12()
        : Window {"Hello, D3D12!", {1600, 900} }
    {
        ComPtr<ID3D12Debug> debug;
        XOR_CHECK_HR(D3D12GetDebugInterface(
            __uuidof(ID3D12Debug),
            &debug));
        debug->EnableDebugLayer();

        ComPtr<IDXGIFactory2> factory;
        XOR_CHECK_HR(CreateDXGIFactory1(
            __uuidof(IDXGIFactory2),
            &factory));
        XOR_CHECK_HR(D3D12CreateDevice(
            nullptr,
            D3D_FEATURE_LEVEL_12_0,
            __uuidof(ID3D12Device),
            &m_device));

        ComPtr<ID3D12DebugDevice> debugDevice;
        XOR_CHECK_HR(m_device.As(&debugDevice));

        {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.NodeMask = 0;
            XOR_CHECK_HR(m_device->CreateCommandQueue(
                &desc,
                __uuidof(ID3D12CommandQueue),
                &m_queue));
        }

        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width              = size().x;
            desc.Height             = size().y;
            desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Stereo             = false;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount        = 3;
            desc.Scaling            = DXGI_SCALING_NONE;
            desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags              = 0;

            ComPtr<IDXGISwapChain1> swapChain;
            XOR_CHECK_HR(factory->CreateSwapChainForHwnd(
                m_queue.Get(),
                hWnd(),
                &desc,
                nullptr,
                nullptr,
                &swapChain));

            XOR_CHECK_HR(swapChain.As(&m_swapChain));
        }

        m_frameNumber = m_swapChain->GetCurrentBackBufferIndex();

        XOR_CHECK_HR(m_device->CreateFence(
            m_frameNumber,
            D3D12_FENCE_FLAG_NONE,
            __uuidof(ID3D12Fence),
            &m_frameFence));

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NodeMask       = 0;
            desc.NumDescriptors = BufferCount;
            desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            XOR_CHECK_HR(m_device->CreateDescriptorHeap(
                &desc,
                __uuidof(ID3D12DescriptorHeap),
                &m_rtvHeap));
        }

        {
            auto currentRTV = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            auto rtvIncrement = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            for (uint i = 0; i < BufferCount; ++i)
            {
                Frame f;
                XOR_CHECK_HR(m_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    __uuidof(ID3D12CommandAllocator),
                    &f.allocator));
                XOR_CHECK_HR(m_device->CreateCommandList(
                    0,
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    f.allocator.Get(),
                    nullptr,
                    __uuidof(ID3D12GraphicsCommandList),
                    &f.cmd));
                f.cmd->Close();

                XOR_CHECK_HR(m_swapChain->GetBuffer(
                    i,
                    __uuidof(ID3D12Resource),
                    &f.rt));

                f.rtv = currentRTV;

                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc ={};
                rtvDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
                rtvDesc.ViewDimension        = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice   = 0;
                rtvDesc.Texture2D.PlaneSlice = 0;
                m_device->CreateRenderTargetView(
                    f.rt.Get(),
                    &rtvDesc,
                    f.rtv);

                currentRTV.ptr += rtvIncrement;

                f.number = m_frameNumber;

                m_frames.emplace_back(std::move(f));
            }
        }
    }

    void mainLoop() override
    {
        auto &f = m_frames[m_frameNumber % BufferCount];
        ++m_frameNumber;

        // Wait until the frame has retired.
        while (m_frameFence->GetCompletedValue() < f.number)
            Sleep(1);

        XOR_CHECK_HR(f.allocator->Reset());
        XOR_CHECK_HR(f.cmd->Reset(f.allocator.Get(), nullptr));

        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = f.rt.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            f.cmd->ResourceBarrier(1, &barrier);
        }

        f.cmd->ClearRenderTargetView(f.rtv, float4(0, 0, 0.25f, 1).data(), 0, nullptr);

        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = f.rt.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            f.cmd->ResourceBarrier(1, &barrier);
        }

        XOR_CHECK_HR(f.cmd->Close());

        f.number = m_frameNumber;

        ID3D12CommandList *cmds[] = { f.cmd.Get() };
        m_queue->ExecuteCommandLists(1, cmds);
        m_queue->Signal(m_frameFence.Get(), f.number);

        m_swapChain->Present(1, 0);
    }
};

int main(int argc, const char *argv[])
{
    HelloD3D12 hello;
    return hello.run();
}

