#include "Core/MathMorton.hpp"

// This #include has "using namespace std" in the header, and has thus
// been contained in this file.
#include "external/libmorton/libmorton/include/morton.h"

namespace xor
{
    uint64_t morton2DEncode(uint2 coords)
    {
        return morton2D_64_encode(coords.x, coords.y);
    }

    uint2 morton2DDecode(uint64_t mortonIndex)
    {
        uint2 coords;
        morton2D_64_decode(mortonIndex, coords.x, coords.y);
        return coords;
    }

    uint64_t morton3DEncode(uint3 coords)
    {
        return morton3D_64_encode(coords.x, coords.y, coords.z);
    }

    uint3 morton3DDecode(uint64_t mortonIndex)
    {
        uint3 coords;
        morton3D_64_decode(mortonIndex, coords.x, coords.y, coords.z);
        return coords;
    }
}