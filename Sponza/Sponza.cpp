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

struct FPSCamera
{
    struct Keys
    {
        int forward   = 0;
        int backward  = 0;
        int left      = 0;
        int right     = 0;
        int lookUp    = 0;
        int lookDown  = 0;
        int lookLeft  = 0;
        int lookRight = 0;
        int moveFast  = 0;
    } keys;

    float3 position = 0;
    Angle azimuth   = Angle(0);
    Angle elevation = Angle(0);
    float speed     = 1;
    float turnSpeed = .2f;

    void update(const Window &window)
    {
        float x = 0;
        float z = 0;
        float s = 1;

        if (window.isKeyHeld(keys.moveFast)) s = 10;

        if (window.isKeyHeld(keys.forward))  z -= 1;
        if (window.isKeyHeld(keys.backward)) z += 1;
        if (window.isKeyHeld(keys.left))     x -= 1;
        if (window.isKeyHeld(keys.right))    x += 1;

        Matrix M = orientation();

        if (x != 0)
        {
            x *= speed * s;
            position += M.getRotationXAxis() * x;
        }

        if (z != 0)
        {
            z *= speed * s;
            position += M.getRotationZAxis() * z;
        }

        if (window.isKeyHeld(keys.lookLeft))  azimuth.radians   += turnSpeed;
        if (window.isKeyHeld(keys.lookRight)) azimuth.radians   -= turnSpeed;
        if (window.isKeyHeld(keys.lookUp))    elevation.radians += turnSpeed;
        if (window.isKeyHeld(keys.lookDown))  elevation.radians -= turnSpeed;
    }

    Matrix orientation() const
    {
        Matrix A = Matrix::axisAngle({0, 1, 0}, azimuth);
        Matrix E = Matrix::axisAngle({1, 0, 0}, elevation);
        return A * E;
    }

    Matrix viewMatrix() const
    {
        Matrix T = Matrix::translation(-position);
        Matrix R = orientation().transpose();
        return R * T;
    }
};

class Sponza : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    TextureDSV depthBuffer;
    GraphicsPipeline basicMesh;
    std::vector<Mesh> meshes;
    FPSCamera camera;

    Timer time;

    struct Parameters
    {
        float3 sunColor     = 1.f;
        float3 sunDirection = { 1.f, 1.f, 1.f };
        float3 ambientColor = .05f;
        float roughness     = 0.5f;
        float F0            = 0.04f;
    } params;

public:
    Sponza()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);
        depthBuffer = device.createTextureDSV(Texture::Info(size(), DXGI_FORMAT_D32_FLOAT));

        Timer loadingTime;
        meshes = Mesh::loadFromFile(device, Mesh::Builder()
                                    .filename(XOR_DATA "/crytek-sponza/sponza.obj")
                                    .loadMaterials(true));
        log("Sponza", "Loaded scene in %.2f ms\n", loadingTime.milliseconds());

        basicMesh = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("BasicMesh.vs")
            .pixelShader("BasicMesh.ps")
            .inputLayout(meshes[0].inputLayout())
            .renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            .depthFormat(DXGI_FORMAT_D32_FLOAT)
            .depthMode(info::DepthMode::Write)
        );

        camera.keys.forward   = 'W';
        camera.keys.left      = 'A';
        camera.keys.backward  = 'S';
        camera.keys.right     = 'D';
        camera.keys.lookUp    = VK_UP;
        camera.keys.lookLeft  = VK_LEFT;
        camera.keys.lookDown  = VK_DOWN;
        camera.keys.lookRight = VK_RIGHT;
        camera.keys.moveFast  = VK_SHIFT;
        camera.position       = { -1000, 500, 0 };
        camera.azimuth        = Angle::degrees(-90);
        camera.speed          = 10;
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
        cmd.clearRTV(backbuffer, float4(0, 0, 0, 1));
        cmd.clearDSV(depthBuffer, 0);

        if (ImGui::Begin("Sponza", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Hello, Sponza!");
            ImGui::InputFloat3("Position", camera.position.data(), 2);
            if (ImGui::Button("Reset"))
            {
                camera.position  = { -1000, 500, 0 };
                camera.azimuth   = Angle::degrees(-90);
                camera.elevation = Angle(0);
            }
            ImGui::InputFloat3("Sun direction", params.sunDirection.data(), 2);
            ImGui::InputFloat3("Sun color",     params.sunColor.data(), 2);
            ImGui::InputFloat3("Ambient color", params.ambientColor.data(), 2);
            ImGui::InputFloat("Roughness", &params.roughness, 2);
            ImGui::InputFloat("F0", &params.F0, 2);
        }
        ImGui::End();

        BasicMesh::Constants constants;
        Matrix MVP =
            Matrix::projectionPerspective(size(),
                                          math::DefaultFov,
                                          .1f, 5000.f)
            * camera.viewMatrix();
            /*
            * Matrix::lookAt({ -1000, 500, 0000 },
                             0);
                             */
        constants.modelViewProj = MVP;

        constants.sunDirection                 = float4(normalize(params.sunDirection));
        constants.sunColor                     = float4(params.sunColor);
        constants.ambientColor                 = float4(params.ambientColor);
        constants.cameraPosition               = float4(camera.position);
        constants.materialProperties.roughness = params.roughness;
        constants.materialProperties.F0        = params.F0;

        cmd.setRenderTargets(backbuffer, depthBuffer);
        cmd.bind(basicMesh);

        for (auto &m : meshes)
        {
            auto mat = m.material();

            m.setForRendering(cmd);
            cmd.setConstants(constants);

            if (mat.albedo().texture)
                cmd.setShaderView(BasicMesh::albedoTex, m.material().albedo().texture);

            cmd.drawIndexed(m.numIndices());
        }

        cmd.setRenderTargets();
        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return Sponza().run();
}
