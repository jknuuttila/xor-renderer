#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"

#include <random>
#include <unordered_set>

using namespace xor;

class LoadBalancing : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;

public:
    LoadBalancing()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
#if 0
        , xor(Xor::DebugLayer::GPUBasedValidation)
#endif
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

#if 1
        device      = xor.defaultDevice();
#else
        device      = xor.warpDevice();
#endif
        swapChain   = device.createSwapChain(*this);
    }

    void handleInput(const Input &input) override
    {
        auto imguiInput = device.imguiInput(input);
    }

    void keyDown(int keyCode) override
    {
        if (keyCode == VK_ESCAPE)
            terminate(0);
    }

    void mainLoop(double deltaTime) override
    {
        auto cmd        = device.graphicsCommandList("Frame");
        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

        cmd.clearRTV(backbuffer, float4(.1f, .1f, .25f, 1.f));

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return LoadBalancing().run();
}
