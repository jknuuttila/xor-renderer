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
    uint vertexCullEnabled;
    float vertexCullNear;
    float vertexCullFar;
    int clusterId;
    float lodSwitchDistance;
    float lodSwitchExponentInvLog;
    float lodBias;
};

XOR_END_SIGNATURE

#if defined(XOR_SHADER)

#include "Xor/ShaderMath.h.hlsl"

struct VSInput
{
    int2  pixelCoords        : POSITION0;
    float height             : POSITION1;
    int2  nextLodPixelCoords : POSITION2;
    float nextLodHeight      : POSITION3;
    float longestEdge        : POSITION4;
};

float2 terrainWorldCoords(int2 pixelCoords)
{
    float2 xy    = pixelCoords - worldCenter;
    float2 world = xy * texelSize;
    return world;
}

bool vertexInLodArea(float2 worldCoords, float longestEdge)
{
    float dist                   = length(cameraWorldCoords - worldCoords);
    float closestVertexEstimate  = max(0, dist - longestEdge);
    float furthestVertexEstimate = dist + longestEdge;
    bool tooFar                  = closestVertexEstimate  > vertexCullFar;
    bool tooNear                 = furthestVertexEstimate < vertexCullNear;
    return !(tooNear || tooFar);
}

float2 terrainUV(int2 pixelCoords)
{
    return float2(pixelCoords) * heightmapInvSize;
}

float terrainLOD(float distance)
{
    float linearLOD        = distance / lodSwitchDistance;
    float logLOD           = lodSwitchExponentInvLog != 0
        ? (log(linearLOD) * lodSwitchExponentInvLog)
        : linearLOD;
    return logLOD + lodBias;
}

float terrainLODAlpha(float distance)
{
    float lod        = terrainLOD(distance);
    float fractional = saturate(lod - float(lodLevel));
    return fractional;
}

struct TerrainVertex
{
    int2  pixelCoords;
    float height;
    int2  nextLodPixelCoords;
    float nextLodHeight;
    float2 loddedPixelCoords;
    float  loddedHeight;
    float longestEdge;

    bool isCulled()
    {
        if (vertexCullEnabled)
            return !vertexInLodArea(worldCoords(), longestEdge);
        else
            return false;
    }

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

TerrainVertex makeTerrainVertex(VSInput vsInput)
{
    TerrainVertex v;
    v.pixelCoords        = vsInput.pixelCoords;
    v.height             = vsInput.height;
    v.nextLodPixelCoords = vsInput.nextLodPixelCoords;
    v.nextLodHeight      = vsInput.nextLodHeight;
    v.longestEdge        = vsInput.longestEdge;

    v.computeLOD();

    return v;
}

#endif

#endif
