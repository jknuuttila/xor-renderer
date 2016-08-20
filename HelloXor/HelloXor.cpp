#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/Mesh.hpp"

#include "Hello.sig.h"

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

class HelloXor : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    GraphicsPipeline hello;
    TextureSRV lena;
    float2 pixel;
    Mesh cube;

    Timer time;
    const float CameraDistance = 3;
    const float CameraPeriod   = 10;
    const float ObjectPeriod   = 3;

public:
    HelloXor()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);

        cube = Mesh(device, Mesh::Info(XOR_DATA "/cube/cube.obj"));

        hello     = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("Hello.vs")
            .pixelShader("Hello.ps")
            .inputLayout(cube.inputLayout())
            .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));

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

        float objectPhase = frac(time.secondsF() / ObjectPeriod) * 2 * Pi;
        float cameraPhase = frac(time.secondsF() / CameraPeriod) * 2 * Pi;

        Hello::Constants c;

        float3 cameraPos;
        cameraPos.x = cos(cameraPhase) * CameraDistance;
        cameraPos.z = sin(cameraPhase) * CameraDistance;
        cameraPos.y = 2;

        Matrix view = Matrix::lookAt(cameraPos, 0);
        Matrix proj = Matrix::projectionPerspective(size());
        c.viewProj = proj * view;
        c.model    = Matrix::axisAngle({1, 0, 0}, Angle(objectPhase));

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
