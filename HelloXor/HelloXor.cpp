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
    BufferVBV vertexBuffer;
    Timer time;

    struct Vertex
    {
        float2 pos;
        float2 uv;
        float3 color;
    };

public:
    HelloXor()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);
        hello     = device.createGraphicsPipeline(
            Pipeline::Graphics()
            .vertexShader("Hello.vs")
            .pixelShader("Hello.ps")
            .inputLayout(info::InputLayoutInfoBuilder()
                         .element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT))
            .renderTargetFormats({DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}));

        static const float D = .75f;
        Vertex vertices[] = {
            { float2(-D, +D), float2(0, 0), float3(1, 0, 0) },
            { float2(-D, -D), float2(0, 1), float3(0, 1, 0) },
            { float2(+D, +D), float2(1, 0), float3(0, 0, 1) },
            { float2(+D, -D), float2(1, 1), float3(1, 0, 1) },
        };

        vertexBuffer = device.createBufferVBV(asConstSpan(vertices));
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
        cmd.setVBV(vertexBuffer);
        cmd.draw(4);
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
