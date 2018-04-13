#include "Core/Core.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"

using namespace Xor;

class RayTracing : public Window
{
    XorLibrary xorLib;
    Device device;
    SwapChain swapChain;
    FPSCamera camera;
    RWImageData image;
    double time = 0;

public:
    RayTracing()
        : Window { XOR_PROJECT_NAME, { 800, 450 } }
    {
        xorLib.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);
#if 1
        device      = xorLib.defaultDevice();
#else
        device      = xorLib.warpDevice();
#endif
        swapChain   = device.createSwapChain(*this);

        image = RWImageData(size(), DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    void handleInput(const Input &input) override
    {
        auto imguiInput = device.imguiInput(input);
    }

    void mainLoop(double deltaTime) override
    {
        time += deltaTime;

        auto cmd        = device.graphicsCommandList("Frame");

        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

        uint2 sz = size();
        for (uint y = 0; y < sz.y; ++y)
        {
            float val = float(y) / sz.y;
            val += float(time);
            val = frac(val);

            uint8_t byteVal = uint8_t(val * 255.0);

            std::array<uint8_t, 4> px = { byteVal, byteVal, byteVal, 255 };

            for (uint x = 0; x < sz.x; ++x)
            {
                image.pixel<std::array<uint8_t, 4>>(uint2(x, y)) = px;
            }
        }

        Texture backbufferTex = backbuffer.texture();
        cmd.updateTexture(backbufferTex, image);

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main()
{
    return RayTracing().run();
}
