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

static const float NearPlane = 1.f;

struct Heightmap
{
    Image image;
    TextureSRV srv;
    int2 size;
    float2 worldSize;
    float texelSize;
    float minHeight = 1e10;
    float maxHeight = -1e10;

    Heightmap() = default;
    Heightmap(Device &device, StringView file, float texelSize = ArcSecond / 3.f)
    {
        image = Image(Image::Builder().filename(file));
        srv   = device.createTextureSRV(Texture::Info(image));
        size  = int2(image.size());
        this->texelSize = texelSize;
        worldSize = texelSize * float2(size);

#if defined(_DEBUG)
        minHeight = 340.f;
        maxHeight = 2600.f;
#else
        Timer t;
        auto size = image.size();
        auto sr   = image.subresource(0);
        for (uint y = 0; y < size.y; ++y)
        {
            for (float f : sr.scanline<float>(y))
            {
                minHeight = std::min(f, minHeight);
                maxHeight = std::max(f, maxHeight);
            }
        }
        log("Heightmap", "Scanned heightmap bounds in %.2f ms\n", t.milliseconds());
#endif
    }

    ProcessingMesh uniformGrid(Rect area, int vertexDistance = 0)
    {
        Timer t;

        area.rightBottom = min(area.rightBottom, size);
        if (all(area.size() < uint2(128)))
            area.leftTop = area.rightBottom - 128;

        int2 sz        = int2(area.size());
        float2 szWorld = float2(sz) * texelSize;

        ProcessingMesh mesh;

        if (vertexDistance <= 0)
        {
            static const int DefaultVertexDim = 1024;
            int vertexDim = (vertexDistance < 0) ? -vertexDistance : DefaultVertexDim;
            int minDim = std::min(sz.x, sz.y);
            vertexDistance = minDim / vertexDim;
        }

        int2 verts = sz / vertexDistance;
        float2 fVerts = float2(verts);
        float2 fRes   = float2(sz);
        float2 topLeft = -szWorld / 2.f;

        auto heightData = image.subresource(0);

        mesh.positions.reserve((verts.x + 1) * (verts.y + 1));

        for (int y = 0; y <= verts.y; ++y)
        {
            for (int x = 0; x <= verts.x; ++x)
            {
                int2 vertexGridCoords = int2(x, y);
                int2 texCoords = min(vertexGridCoords * vertexDistance + area.leftTop, size - 1);
                float2 uv = float2(vertexGridCoords * vertexDistance) / fRes;

                float3 pos;
                pos.s_xz = uv * szWorld + topLeft;
                pos.y = heightData.pixel<float>(uint2(texCoords));
                mesh.positions.emplace_back(pos);
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
    GraphicsPipeline renderTerrain;
    Mesh mesh;
    int2 areaStart = { 2000, 0 };
    int areaSize  = 2048;
    bool blitArea  = true;
    bool wireframe = false;

public:
    Terrain()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);
        depthBuffer = device.createTextureDSV(Texture::Info(size(), DXGI_FORMAT_D32_FLOAT));
        blit = Blit(device);

        Timer loadingTime;

        heightmap      = Heightmap(device, XOR_DATA "/heightmaps/grand-canyon/floatn36w114_13.flt");
        updateTerrain();
        renderTerrain  = device.createGraphicsPipeline(GraphicsPipeline::Info()
                                                      .vertexShader("RenderTerrain.vs")
                                                      .pixelShader("RenderTerrain.ps")
                                                      .depthMode(info::DepthMode::Write)
                                                      .depthFormat(DXGI_FORMAT_D32_FLOAT)
                                                      .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                                                      .inputLayout(mesh.inputLayout()));

        camera.speed /= 10;
        camera.fastMultiplier *= 5;
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

    void updateTerrain()
    {
        mesh = heightmap.uniformGrid(Rect::withSize(areaStart, areaSize)).mesh(device);
        camera.position = float3(0, heightmap.maxHeight + NearPlane * 10, 0);
    }

    void mainLoop(double deltaTime) override
    {
        camera.update(*this);

        auto cmd        = device.graphicsCommandList("Frame");
        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

        if (ImGui::Begin("Terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SliderInt("Size", &areaSize, 0, heightmap.size.x);
            ImGui::SliderInt2("Start", areaStart.data(), 0, heightmap.size.x - areaSize);
            ImGui::Checkbox("Show area", &blitArea);
            ImGui::Checkbox("Wireframe", &wireframe);

            if (ImGui::Button("Update"))
                updateTerrain();

            ImGui::End();
        }

        {
            auto p = cmd.profilingEvent("Clear");
            cmd.clearRTV(backbuffer, float4(0, 0, 0, 1));
            cmd.clearDSV(depthBuffer, 0);
        }

        cmd.setRenderTargets(backbuffer, depthBuffer);
        cmd.bind(renderTerrain);
        RenderTerrain::Constants constants;
        constants.viewProj =
            Matrix::projectionPerspective(backbuffer.texture()->size, math::DefaultFov,
                                          1.f, heightmap.worldSize.x * 1.5f)
            * camera.viewMatrix();
        constants.heightMin = heightmap.minHeight;
        constants.heightMax = heightmap.maxHeight;
        constants.wireframe = 0;

        cmd.setConstants(constants);
        mesh.setForRendering(cmd);
        {
            auto p = cmd.profilingEvent("Draw opaque");
            cmd.drawIndexed(mesh.numIndices());
        }

        if (wireframe)
        {
            auto p = cmd.profilingEvent("Draw wireframe");
            cmd.bind(renderTerrain.variant()
                     .pixelShader(info::SameShader {}, { { "WIREFRAME" } })
                     .depthMode(info::DepthMode::ReadOnly)
                     .depthBias(10000)
                     .fill(D3D12_FILL_MODE_WIREFRAME));
            cmd.setConstants(constants);
            cmd.drawIndexed(mesh.numIndices());
        }

        cmd.setRenderTargets();

        if (blitArea)
        {
            auto p = cmd.profilingEvent("Blit heightmap");
            float2 norm = normalizationMultiplyAdd(heightmap.minHeight, heightmap.maxHeight);

            blit.blit(cmd,
                      backbuffer, Rect::withSize(int2(backbuffer.texture()->size - 300).s_x0, 300),
                      heightmap.srv, Rect::withSize(areaStart, areaSize),
                      norm.s_x000, norm.s_y001);
        }

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return Terrain().run();
}
