#ifndef TERRAIN_PATCH_CONSTANTS_H
#define TERRAIN_PATCH_CONSTANTS_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(TerrainRendering)

XOR_CBUFFER(Constants, 0)
{
    int2 worldMin;
    int2 worldMax;
    float2 worldCenter;
    float2 heightmapInvSize;
    float heightMin;
    float heightMax;
    float texelSize;
    int lodLevel;
    float2 cameraWorldCoords;
    int lodEnabled;
    float lodSwitchDistance;
    float lodSwitchExponentInvLog;
    float lodBias;
    float lodMorphStart;
};

XOR_END_SIGNATURE

#if defined(XOR_SHADER)

#include "Xor/ShaderMath.h.hlsl"

float2 terrainWorldCoords(int2 pixelCoords)
{
    float2 xy    = pixelCoords - worldCenter;
    float2 world = xy * texelSize;
    return world;
}

float2 terrainUV(int2 pixelCoords)
{
    return float2(pixelCoords) * heightmapInvSize;
}

float terrainLOD(float sampledLOD, float distance)
{
#if 1
    return sampledLOD;
#else
    float linearLOD        = distance / lodSwitchDistance;
    float logLOD           = lodSwitchExponentInvLog != 0
        ? (log(linearLOD) * lodSwitchExponentInvLog)
        : linearLOD;
    return logLOD + lodBias;
#endif
}

float terrainLODAlpha(float sampledLOD, float distance)
{
    float lod        = terrainLOD(sampledLOD, distance);
    float fractional = saturate(lod - float(lodLevel));
#if 1
    float alpha      = saturate(remap(lodMorphStart, 1, 0, 1, fractional));
#else
    float alpha      = smoothstep(lodMorphStart, 1.0, fractional);
#endif
    return alpha;
}

struct TerrainVertex
{
    int2  pixelCoords;
    float height;
    int2  nextLodPixelCoords;
    float nextLodHeight;
    float2 loddedPixelCoords;
    float  loddedHeight;

    float2 worldCoords()
    {
        return terrainWorldCoords(loddedPixelCoords);
    }

    float2 uv()
    {
        return terrainUV(loddedPixelCoords);
    }

    float2 normalizedPos()
    {
        float2 minUV = terrainUV(worldMin);
        float2 maxUV = terrainUV(worldMax);
        return remap(minUV, maxUV, 0, 1, terrainUV(loddedPixelCoords));
    }

    void computeLOD()
    {
        float distanceToCamera = length(terrainWorldCoords(pixelCoords) - cameraWorldCoords);

        loddedPixelCoords = pixelCoords;
        loddedHeight      = height;

#if 0
        if (lodEnabled)
        {
            float alpha = terrainLODAlpha(terrainSampleLOD(uv()),
                                          distanceToCamera);

            float2 morphPixelCoords = lerp(pixelCoords, nextLodPixelCoords, alpha);
            float  morphHeight      = lerp(height, nextLodHeight, alpha);

            loddedPixelCoords = morphPixelCoords;
            loddedHeight      = morphHeight;
        }
#endif
    }
};

TerrainVertex makeTerrainVertex(int2 pixelCoords, float height,
                                int2 nextLodPixelCoords, float nextLodHeight)
{
    TerrainVertex v;
    v.pixelCoords        = pixelCoords;
    v.height             = height;
    v.nextLodPixelCoords = nextLodPixelCoords;
    v.nextLodHeight      = nextLodHeight;

    v.computeLOD();

    return v;
}

#endif

#endif
