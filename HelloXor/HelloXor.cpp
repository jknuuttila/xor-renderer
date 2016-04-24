#include "Core/Core.hpp"
#include "Xor/Xor.hpp"

using namespace xor;

class HelloXor : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    Timer time;
public:
    HelloXor()
        : Window { "Hello, Xor!", { 1600, 900 } }
    {
        device    = xor.defaultAdapter().createDevice();
        swapChain = device.createSwapChain(*this);
    }

    void keyDown(int keyCode) override
    {
        if (keyCode == VK_ESCAPE)
            terminate(0);
    }

    void mainLoop() override
    {
        auto cmd        = device.graphicsCommandList();
        auto backbuffer = swapChain.backbuffer();

        // TODO: Replace transition() with automatic deduction of split barriers.
        cmd.barrier({ transition(backbuffer.texture(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET) });

        float4 rgba = float4(hsvToRGB(float3(
            frac(static_cast<float>(time.seconds())),
            1,
            1)));
        rgba.w = 1;
        cmd.clearRTV(backbuffer, rgba);

        cmd.barrier({ transition(backbuffer.texture(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT) });

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return HelloXor().run();
}
