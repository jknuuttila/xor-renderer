#include "Core/Core.hpp"
#include "Xor/Xor.hpp"

using namespace xor;

class HelloXor : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
public:
    HelloXor()
        : Window { "Hello, Xor!", { 1600, 900 } }
    {
        device    = xor.defaultAdapter().createDevice();
        swapChain = device.createSwapChain(*this);
    }

    void keyDown(int keyCode) override
    {
        if (keyCode == VK_ESCAPE)
            terminate(0);
    }
};

int main(int argc, const char *argv[])
{
    return HelloXor().run();
}
