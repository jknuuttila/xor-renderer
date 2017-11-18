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
    float2 worldCenter;
    float2 heightmapInvSize;
    float heightMin;
    float heightMax;
    float texelSize;
    int tileLOD;
    float2 cameraWorldCoords;
    float lodMorphMinDistance;
    float lodMorphMaxDistance;
    int lodEnabled;
};

XOR_END_SIGNATURE

#ifdef XOR_SHADER

#include "Xor/ShaderMath.h.hlsl"

float2 terrainUV(int2 pixelCoords)
{
    return float2(pixelCoords) * heightmapInvSize;
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
        float2 xy    = loddedPixelCoords - worldCenter;
        float2 world = xy * texelSize;
        return world;
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
        float distanceToCamera = length(float2(pixelCoords) - cameraWorldCoords);

        loddedPixelCoords = pixelCoords;
        loddedHeight      = height;

        if (lodEnabled)
        {
            float alpha;

            if (lodMorphMinDistance == lodMorphMaxDistance)
            {
                alpha = (distanceToCamera < lodMorphMaxDistance)
                    ? 0 : 1;
            }
            else
            {
                alpha = smoothstep(lodMorphMinDistance, lodMorphMaxDistance, distanceToCamera);
            }

            float2 morphPixelCoords = lerp(pixelCoords, nextLodPixelCoords, alpha);
            float  morphHeight      = lerp(height, nextLodHeight, alpha);

            loddedPixelCoords = morphPixelCoords;
            loddedHeight      = morphHeight;
        }
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
