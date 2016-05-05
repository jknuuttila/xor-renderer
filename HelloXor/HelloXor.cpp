#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"

using namespace xor;

class HelloXor : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    Pipeline hello;
    Timer time;
public:
    HelloXor()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        device    = xor.defaultAdapter().createDevice();
        swapChain = device.createSwapChain(*this);
        hello     = device.createGraphicsPipeline(
            Pipeline::Graphics()
            .vertexShader("Hello.vs")
            .pixelShader("Hello.ps")
            .renderTargetFormats({DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}));

        scanBuildInfos(XOR_PROJECT_TLOG, ".cso");
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
        cmd.barrier({ transition(backbuffer.texture(),
                                 D3D12_RESOURCE_STATE_PRESENT,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET) });

#if 0
        float4 rgba = float4(hsvToRGB(float3(
            frac(static_cast<float>(time.seconds())),
            1,
            1)));
        rgba.w = 1;
        cmd.clearRTV(backbuffer, rgba);
#else
        cmd.clearRTV(backbuffer, float4(0, 0, .25f, 1));

        cmd.setRenderTargets({backbuffer});
        cmd.bind(hello);
        cmd.draw(3);
        cmd.setRenderTargets();

#endif

        cmd.barrier({ transition(backbuffer.texture(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_PRESENT) });

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return HelloXor().run();
}
