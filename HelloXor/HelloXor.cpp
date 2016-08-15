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
    BufferVBV vertexBuffer;
    BufferIBV indexBuffer;
    TextureSRV lena;
    Timer time;
    float2 pixel;

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
            GraphicsPipeline::Info()
            .vertexShader("Hello.vs")
            .pixelShader("Hello.ps")
            .inputLayout(info::InputLayoutInfoBuilder()
                         .element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT)
                         .element("COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT))
            .renderTargetFormats({DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}));

        pixel = float2(2) / float2(size());
        lena = device.createTextureSRV(Image(XOR_DATA "/Lena.png"));

        auto lenaSize = float2(lena.texture()->size) * pixel;

        Vertex vertices[] = {
            { float2(0.5f) * float2(-1, +1) * lenaSize, float2(0, 0), float3(1, 0, 0) },
            { float2(0.5f) * float2(-1, -1) * lenaSize, float2(0, 1), float3(0, 1, 0) },
            { float2(0.5f) * float2(+1, +1) * lenaSize, float2(1, 0), float3(0, 0, 1) },
            { float2(0.5f) * float2(+1, -1) * lenaSize, float2(1, 1), float3(1, 0, 1) },
        };

        auto meshes = Mesh::loadFromFile(device, XOR_DATA "/crytek-sponza/sponza.obj");
        //auto meshes = Mesh::loadFromFile(device, XOR_DATA "/teapot/teapot.obj");

        vertexBuffer = device.createBufferVBV(asConstSpan(vertices));
        indexBuffer  = device.createBufferIBV(
            Buffer::Info({ 0, 1, 2, 1, 3, 2, }, DXGI_FORMAT_R32_UINT));

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
        cmd.setVBV(vertexBuffer);
        cmd.setIBV(indexBuffer);
        cmd.setTopology();

        Hello::OffsetConstants offset;
        Hello::SizeConstants   size;

        offset.offset = float2(-500, +150) * pixel;
        size.size     = float2(1);

        cmd.setConstants(offset);
        cmd.setConstants(size);
        cmd.setShaderView(Hello::tex, lena);
        cmd.drawIndexed(6);

        offset.offset = float2(+500, +150) * pixel;
        size.size     = float2(0.5f);

        cmd.setConstants(offset);
        cmd.setConstants(size);
        cmd.setShaderView(Hello::tex, lena);
        cmd.drawIndexed(6);

        cmd.setRenderTargets();

        cmd.copyTexture(backbuffer.texture(), { { 600, 350 } },
                        lena.texture());

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return HelloXor().run();
}
