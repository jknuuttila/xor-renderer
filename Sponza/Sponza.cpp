#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"

#include "BasicMesh.sig.h"

using namespace xor;

void debugMatrix(Matrix m, Span<const float3> verts)
{
    std::vector<float3> t;

    for (auto v : verts)
    {
        t.emplace_back(m.transformAndProject(v));
        print("%s -> %s\n", toString(v).cStr(), toString(t.back()).cStr());
    }
}

class Sponza : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    GraphicsPipeline basicMesh;

    Timer time;

public:
    Sponza()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);

        basicMesh = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("BasicMesh.vs")
            .pixelShader("BasicMesh.ps")
            .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
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

        cmd.clearRTV(backbuffer, float4(.25f, 0, 0, 1));

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return Sponza().run();
}
