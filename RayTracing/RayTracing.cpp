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

public:
    RayTracing()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xorLib.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);
#if 1
        device      = xorLib.defaultDevice();
#else
        device      = xorLib.warpDevice();
#endif
        swapChain   = device.createSwapChain(*this);
    }
};

int main()
{
    return RayTracing().run();
}
