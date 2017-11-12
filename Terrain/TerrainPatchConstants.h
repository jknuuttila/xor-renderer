#ifndef TERRAIN_PATCH_CONSTANTS_H
#define TERRAIN_PATCH_CONSTANTS_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(TerrainPatch)

XOR_CBUFFER(Constants, 0)
{
    int2 tileMin;
    int2 tileMax;
    int2 worldMin;
    int2 worldMax;
    int2 worldCenter;
    float2 heightmapInvSize;
    float heightMin;
    float heightMax;
    float texelSize;
    int tileLOD;
};

XOR_END_SIGNATURE

#ifdef XOR_SHADER

#include "Xor/ShaderMath.h.hlsl"

float2 terrainWorldCoords(int2 pixelCoords)
{
    int2 xy = pixelCoords - worldCenter;
    float2 world = float2(xy) * texelSize;
    return world;
}
float2 terrainUV(int2 pixelCoords)
{
    return float2(pixelCoords) * heightmapInvSize;
}
float2 terrainNormalizedPos(int2 pixelCoords)
{
    float2 minUV = terrainUV(worldMin);
    float2 maxUV = terrainUV(worldMax);
    return remap(minUV, maxUV, 0, 1, terrainUV(pixelCoords));
}
#endif

#endif
