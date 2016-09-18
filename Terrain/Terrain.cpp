#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"

using namespace xor;

class Terrain : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    TextureDSV depthBuffer;
    FPSCamera camera;

    Timer time;

public:
    Terrain()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);
        depthBuffer = device.createTextureDSV(Texture::Info(size(), DXGI_FORMAT_D32_FLOAT));

        Timer loadingTime;
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

        cmd.clearRTV(backbuffer, float4(0, 0, 0.25f, 1));
        cmd.clearDSV(depthBuffer, 0);

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return Terrain().run();
}
