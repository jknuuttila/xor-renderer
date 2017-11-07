#ifndef TERRAIN_PATCH_CONSTANTS_H
#define TERRAIN_PATCH_CONSTANTS_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(TerrainPatch)

XOR_CBUFFER(Constants, 0)
{
    float2 tileMin;
    float2 tileMax;
    float heightMin;
    float heightMax;
};

XOR_END_SIGNATURE

#endif
