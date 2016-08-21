#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/Mesh.hpp"
#include "Xor/Material.hpp"

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
    TextureDSV depthBuffer;
    GraphicsPipeline basicMesh;
    std::vector<Mesh> meshes;

    Timer time;

public:
    Sponza()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);
        depthBuffer = device.createTextureDSV(Texture::Info(size(), DXGI_FORMAT_D32_FLOAT));

        meshes = Mesh::loadFromFile(device, Mesh::Builder()
                                    .filename(XOR_DATA "/crytek-sponza/sponza.obj")
                                    .loadMaterials(true));

        basicMesh = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("BasicMesh.vs")
            .pixelShader("BasicMesh.ps")
            // .cull(D3D12_CULL_MODE_NONE)
            .inputLayout(meshes[0].inputLayout())
            .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            .depthFormat(DXGI_FORMAT_D32_FLOAT)
            .depthMode(info::DepthMode::Write)
        );
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

        cmd.clearRTV(backbuffer, float4(0, 0, 0, 1));

        BasicMesh::Constants constants;
        Matrix MVP =
            Matrix::projectionPerspective(size(),
                                          math::DefaultFov,
                                          1.f, 10000.f)
            * Matrix::lookAt({ 2000, 2000, 2000 },
                             0);
        constants.modelViewProj = MVP;

        cmd.setRenderTargets(backbuffer, depthBuffer);
        cmd.bind(basicMesh);

        for (auto &m : meshes)
        {
            auto mat = m.material();

            m.setForRendering(cmd);
            cmd.setConstants(constants);

            if (mat.diffuse().texture)
                cmd.setShaderView(BasicMesh::diffuseTex, m.material().diffuse().texture);

            cmd.drawIndexed(m.numIndices());
        }

        cmd.setRenderTargets();

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return Sponza().run();
}
