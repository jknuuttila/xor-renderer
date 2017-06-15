#pragma once

#include "Core/MathVectors.hpp"

namespace xor
{
    uint64_t morton2DEncode(uint2 coords);
    uint2    morton2DDecode(uint64_t mortonIndex);
    uint64_t morton3DEncode(uint3 coords);
    uint3    morton3DDecode(uint64_t mortonIndex);
}