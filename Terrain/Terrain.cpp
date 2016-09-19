#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"
#include "Xor/Blit.hpp"
#include "Xor/Mesh.hpp"
#include "Xor/ProcessingMesh.hpp"

#include "RenderTerrain.sig.h"

using namespace xor;

static const float ArcSecond = 30.87f;

struct Heightmap
{
    Image image;
    TextureSRV srv;
    int2 size;
    float2 worldSize;
    float min = 1e10;
    float max = -1e10;

    Heightmap() = default;
    Heightmap(Device &device, StringView file, float texelSize = ArcSecond / 3.f)
    {
        image = Image(Image::Builder().filename(file));
        srv   = device.createTextureSRV(Texture::Info(image));
        size  = int2(image.size());
        worldSize = texelSize * float2(size);

#if defined(_DEBUG)
        min = 340.f;
        max = 2600.f;
#else
        Timer t;
        auto size = image.size();
        auto sr   = image.subresource(0);
        for (uint y = 0; y < size.y; ++y)
        {
            for (float f : sr.scanline<float>(y))
            {
                min = std::min(f, min);
                max = std::max(f, max);
            }
        }
        log("Heightmap", "Scanned heightmap bounds in %.2f ms\n", t.milliseconds());
#endif
    }

    ProcessingMesh uniformGrid(int vertexDistance = 0)
    {
        Timer t;
        ProcessingMesh mesh;

        if (vertexDistance <= 0)
        {
            static const int DefaultVertexDim = 2048;
            int minDim = std::min(size.x, size.y);
            vertexDistance = minDim / DefaultVertexDim;
        }

        static const float Divisor = 10;

        int2 verts = size / vertexDistance;
        float2 fVerts = float2(verts);
        float2 fRes   = float2(size);
        float2 topLeft = -worldSize / 2.f / Divisor;

        auto heightData = image.subresource(0);

        mesh.positions.reserve((verts.x + 1) * (verts.y + 1));

        float minY = 1e10;
        float maxY = -1e10;

        for (int y = 0; y <= verts.y; ++y)
        {
            for (int x = 0; x <= verts.x; ++x)
            {
                int2 coords = int2(x, y);
                float2 uv = float2(coords * vertexDistance) / fRes;

                float3 pos = float3(uv * worldSize) + float3(topLeft);
                pos.z = pos.y;
                pos.y = heightData.pixel<float>(uint2(coords));
                pos.x /= Divisor;
                pos.z /= Divisor;
                mesh.positions.emplace_back(pos);
                minY = std::min(pos.y, minY);
                maxY = std::max(pos.y, maxY);
            }
        }

        int vertsPerRow = verts.y + 1;
        mesh.indices.reserve(verts.x * verts.y * (3 * 2));
        for (int y = 0; y < verts.y; ++y)
        {
            for (int x = 0; x < verts.x; ++x)
            {
                uint ul = y * vertsPerRow + x;
                uint ur = ul + 1;
                uint dl = ul + vertsPerRow;
                uint dr = dl + 1;

                mesh.indices.emplace_back(ul);
                mesh.indices.emplace_back(dl);
                mesh.indices.emplace_back(ur);
                mesh.indices.emplace_back(dl);
                mesh.indices.emplace_back(dr);
                mesh.indices.emplace_back(ur);
            }
        }

        log("Heightmap", "Generated uniform grid mesh with %zu vertices and %zu indices in %.2f ms\n",
            mesh.positions.size(),
            mesh.indices.size(),
            t.milliseconds());

        return mesh;
    }
};

class Terrain : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    TextureDSV depthBuffer;
    FPSCamera camera;
    Blit blit;

    Timer time;

    Heightmap heightmap;
    Mesh mesh;
    GraphicsPipeline renderTerrain;

public:
    Terrain()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);
        depthBuffer = device.createTextureDSV(Texture::Info(size(), DXGI_FORMAT_D32_FLOAT));
        // blit = Blit(device);

        Timer loadingTime;

        heightmap = Heightmap(device, XOR_DATA "/heightmaps/grand-canyon/floatn36w114_13.flt");
        mesh      = heightmap.uniformGrid().mesh(device);
        renderTerrain = device.createGraphicsPipeline(GraphicsPipeline::Info()
                                                      .vertexShader("RenderTerrain.vs")
                                                      .pixelShader("RenderTerrain.ps")
                                                      .cull(D3D12_CULL_MODE_NONE)
                                                      .depthMode(info::DepthMode::Write)
                                                      .depthFormat(DXGI_FORMAT_D32_FLOAT)
                                                      .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                                                      .inputLayout(mesh.inputLayout()));

        camera.position = float3(0, heightmap.max + 100.f, 0);
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
        camera.update(*this);

        auto cmd        = device.graphicsCommandList();
        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

#if 0
        if (ImGui::Begin("Terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SliderInt2("Coords", blitDst.data(), 0, heightmap.srv.texture()->size.x);
            ImGui::End();
        }
#endif

        cmd.clearRTV(backbuffer, float4(0, 0, 0, 1));
        cmd.clearDSV(depthBuffer, 0);

        // (b - a) * s + a = x
        // (b - a) * s = x - a
        // s = (x - a) / (b - a)
        // s = x / (b - a) - a / (b - a)

        cmd.setRenderTargets(backbuffer, depthBuffer);
        cmd.bind(renderTerrain);
        RenderTerrain::Constants constants;
        constants.viewProj = Matrix::projectionPerspective(backbuffer.texture()->size, math::DefaultFov, 100.f, heightmap.max * 2)
            * camera.viewMatrix();
        constants.heightMin = heightmap.min;
        constants.heightMax = heightmap.max;

        cmd.setConstants(constants);
        mesh.setForRendering(cmd);
        cmd.drawIndexed(mesh.numIndices());

        cmd.setRenderTargets();

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return Terrain().run();
}
