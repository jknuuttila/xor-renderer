#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"

#include "Hello.sig.h"

using namespace xor;

class HelloXor : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    GraphicsPipeline hello;
    TextureSRV lena;
    Timer time;
    float2 pixel;
    Mesh cube;

public:
    HelloXor()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);

        cube = Mesh(device, XOR_DATA "/cube/cube.obj");

        hello     = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("Hello.vs")
            .pixelShader("Hello.ps")
            .inputLayout(cube.inputLayout())
            .renderTargetFormats({DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}));

        lena = device.createTextureSRV(Image(XOR_DATA "/Lena.png"));
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

        cmd.clearRTV(backbuffer, float4(0, 0, .25f, 1));

        cmd.setRenderTargets({backbuffer});
        cmd.bind(hello);
        cube.setForRendering(cmd);

        Hello::Constants c;

        c.viewProj =
            Matrix::projectionPerspective(size()) *
            Matrix::lookAt({ -2, 5, -2 }, 0);
        c.model    = Matrix::identity();

        cmd.setConstants(c);
        cmd.setShaderView(Hello::tex, lena);
        cmd.drawIndexed(cube.numIndices());

        cmd.setRenderTargets();

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return HelloXor().run();
}
