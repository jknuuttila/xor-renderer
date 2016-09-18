#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"
#include "Xor/Blit.hpp"

using namespace xor;

class Terrain : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    TextureDSV depthBuffer;
    FPSCamera camera;
    Blit blit;

    Timer time;

    TextureSRV hm;
    float minHeight = 200;
    float maxHeight = 2500;

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

        Image heightmap(Image::Builder().filename(XOR_DATA "/heightmaps/grand-canyon/floatn36w114_13.flt"));
        hm = device.createTextureSRV(heightmap);
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

        // (b - a) * s + a = x
        // (b - a) * s = x - a
        // s = (x - a) / (b - a)
        // s = x / (b - a) - a / (b - a)
        float heightRange = maxHeight - minHeight;
        blit.blit(cmd, backbuffer, 0, hm, ImageRect(int2(0), int2(900, 900)),
                  float4(1 / heightRange, 0, 0, 0),
                  float4(minHeight / heightRange, 0, 0, 1));

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return Terrain().run();
}
