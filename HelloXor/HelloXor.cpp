#include "Core/Core.hpp"
#include "Xor/Xor.hpp"

using namespace xor;

int main(int argc, const char *argv[])
{
    Xor xor;
    Device device = xor.defaultAdapter().createDevice();
    return 0;
}