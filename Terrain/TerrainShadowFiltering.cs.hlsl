#include "TerrainShadowFiltering.sig.h"
#include "Core/SortingNetworks.h"

#ifndef TSF_BILATERAL
#define TSF_BILATERAL 0
#endif

#ifndef TSF_FILTER_WIDTH
#define TSF_FILTER_WIDTH 1
#endif

#ifndef TSF_NEIGHBORHOOD_CLAMP
#define TSF_NEIGHBORHOOD_CLAMP 0
#endif

static const float GaussianFilterWeights[5][6] =
{
      2,   1,   0,   0,   0,   0,
      6,   4,   1,   0,   0,   0,
     20,  15,   6,   1,   0,   0,
     70,  56,  28,   8,   1,   0,
    252, 210, 120,  45,  10,   1,
};

float gaussianFilter(int2 coords)
{
    static const uint Weights = TSF_FILTER_WIDTH - 1;

    float shadowFiltered = 0;
    float totalWeight    = 0;

    for (int dy = -TSF_FILTER_WIDTH; dy <= TSF_FILTER_WIDTH; ++dy)
    {
        int ay = abs(dy);
        float wy = GaussianFilterWeights[Weights][ay];

        for (int dx = -TSF_FILTER_WIDTH; dx <= TSF_FILTER_WIDTH; ++dx)
        {
            int ax = abs(dx);
            float wx = GaussianFilterWeights[Weights][ax];

            float w = wx * wy;
            float s = shadowIn[coords + int2(dx, dy)];

            shadowFiltered += w * s;
            totalWeight    += w;
        }
    }

    shadowFiltered /= totalWeight;

    return shadowFiltered;
}


float medianFilter(int2 coords)
{
    float tmp;
    // TODO: support other sizes
    float s0 = shadowIn[coords + int2(-1, -1)];
    float s1 = shadowIn[coords + int2( 0, -1)];
    float s2 = shadowIn[coords + int2(+1, -1)];
    float s3 = shadowIn[coords + int2(-1,  0)];
    float s4 = shadowIn[coords + int2( 0,  0)];
    float s5 = shadowIn[coords + int2(+1,  0)];
    float s6 = shadowIn[coords + int2(-1, +1)];
    float s7 = shadowIn[coords + int2( 0, +1)];
    float s8 = shadowIn[coords + int2(+1, +1)];

#define SWAP(a, b) if ((s ## b) < (s ## a)) { tmp = s ## a; s ## a = s ## b; s ## b = tmp; }
    XOR_SORTING_NETWORK_9;
#undef SWAP

    return s4;
}

float temporalFilter(int2 coords)
{
    float s0 = shadowIn[coords + int2(-1, -1)];
    float s1 = shadowIn[coords + int2( 0, -1)];
    float s2 = shadowIn[coords + int2(+1, -1)];
    float s3 = shadowIn[coords + int2(-1,  0)];
    float s4 = shadowIn[coords + int2( 0,  0)];
    float s5 = shadowIn[coords + int2(+1,  0)];
    float s6 = shadowIn[coords + int2(-1, +1)];
    float s7 = shadowIn[coords + int2( 0, +1)];
    float s8 = shadowIn[coords + int2(+1, +1)];

    float2 motion = motionVectors[coords];

    float2 currentUV  = (float2(coords) + 0.5) / resolution;
    float2 previousUV = currentUV - motion;

    float currentShadow  = s4;
    float previousShadow = shadowHistory.SampleLevel(bilinearSampler, previousUV, 0);

#if TSF_NEIGHBORHOOD_CLAMP
    float sMin = min(s0, s1);
    sMin       = min(sMin, s2);
    sMin       = min(sMin, s3);
    sMin       = min(sMin, s4);
    sMin       = min(sMin, s5);
    sMin       = min(sMin, s6);
    sMin       = min(sMin, s7);
    sMin       = min(sMin, s8);

    float sMax = max(s0, s1);
    sMax       = max(sMax, s2);
    sMax       = max(sMax, s3);
    sMax       = max(sMax, s4);
    sMax       = max(sMax, s5);
    sMax       = max(sMax, s6);
    sMax       = max(sMax, s7);
    sMax       = max(sMax, s8);

    previousShadow = clamp(previousShadow, sMin, sMax);
#endif

    float shadow = lerp(currentShadow, previousShadow, shadowHistoryBlend);

    return shadow;
}

[RootSignature(TERRAINSHADOWFILTERING_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid    : SV_DispatchThreadID,
          uint  gIndex : SV_GroupIndex,
          uint3 gtid   : SV_GroupThreadID,
          uint3 gid    : SV_GroupID)
{
    float shadow;
#if defined(TSF_FILTER_GAUSSIAN)
    shadow = gaussianFilter(tid.xy);
#elif defined(TSF_FILTER_MEDIAN)
    shadow = medianFilter(tid.xy);
#elif defined(TSF_FILTER_TEMPORAL)
    shadow = temporalFilter(tid.xy);
#else
    shadow = 1;
#endif

    shadowOut[tid.xy] = shadow;
}

#if 0
static const uint TsfTileSize   = TSF_THREADGROUP_SIZE + (2 * TSF_FILTER_WIDTH);
static const uint NumThreads    = TSF_THREADGROUP_SIZE * TSF_THREADGROUP_SIZE;
static const uint NumLdsSamples = TsfTileSize * TsfTileSize;

groupshared float ldsShadow[TsfTileSize][TsfTileSize];

[RootSignature(TERRAINSHADOWFILTERING_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid    : SV_DispatchThreadID,
          uint  gIndex : SV_GroupIndex,
          uint3 gtid   : SV_GroupThreadID,
          uint3 gid    : SV_GroupID)
{
    static const float RcpRowSize = 1.f / TsfTileSize;

    int2 ldsBaseCoords = (gid.xy * TSF_THREADGROUP_SIZE) - TSF_FILTER_WIDTH;

    for (uint i = 0; i < NumLdsSamples; i += NumThreads)
    {
        uint j       = i + gIndex;
        float myItem = float(j) * RcpRowSize;

        int2 ldsCoords;
        ldsCoords.y  = int(myItem);
        ldsCoords.x  = int(round(frac(myItem) * TsfTileSize));

        int2 texCoords = ldsCoords + ldsBaseCoords;
        texCoords      = clamp(texCoords, 0, resolution - 1);

        float shadow   = shadowTerm[texCoords];

        ldsShadow[ldsCoords.y][ldsCoords.x] = shadow;
    }

    float neighborhoodMin = 1000;
    float neighborhoodMax = 0;

    for (i = 0; i < NumLdsSamples; i += NumThreads)
    {
        uint j       = i + gIndex;
        float myItem = float(j) * RcpRowSize;

        int2 ldsCoords;
        ldsCoords.y  = int(myItem);
        ldsCoords.x  = int(round(frac(myItem) * TsfTileSize));

        int2 texCoords = ldsCoords + ldsBaseCoords;
        texCoords      = clamp(texCoords, 0, resolution - 1);

        [unroll]
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int2 pos = ldsCoords + int2(dx, dy);
                pos = clamp(pos, 0, TsfTileSize - 1);

                float s = ldsShadow[pos.y][pos.x];
                neighborhoodMin = min(s, neighborhoodMin);
                neighborhoodMax = max(s, neighborhoodMax);
            }
        }
    }

    for (i = 0; i < NumLdsSamples; i += NumThreads)
    {
        uint j       = i + gIndex;
        float myItem = float(j) * RcpRowSize;

        int2 ldsCoords;
        ldsCoords.y  = int(myItem);
        ldsCoords.x  = int(round(frac(myItem) * TsfTileSize));

        int2 texCoords = ldsCoords + ldsBaseCoords;
        texCoords      = clamp(texCoords, 0, resolution - 1);

        float2 motionVector  = motionVectors[texCoords];
        float2 screenUV      = (float2(texCoords) + 0.5) / float2(resolution);
        float2 reprojectedUV = screenUV + motionVector;

        bool notInPenumbra = false;//centerShadow < .001 || centerShadow > .999;
        bool previousOutOfScreen = any(reprojectedUV != saturate(reprojectedUV));

        float shadow = ldsShadow[ldsCoords.y][ldsCoords.x];

        float finalShadow;

        if (notInPenumbra || previousOutOfScreen)
        {
            finalShadow = shadow;
        }
        else
        {
            float previousShadow = shadowHistory.SampleLevel(bilinearSampler, reprojectedUV, 0);
            previousShadow = clamp(previousShadow, neighborhoodMin, neighborhoodMax);
            finalShadow = lerp(shadow, previousShadow, shadowHistoryBlend);
        }

        ldsShadow[ldsCoords.y][ldsCoords.x] = finalShadow;
    }

    GroupMemoryBarrierWithGroupSync();

    int2 coords    = tid.xy;
    int2 ldsCoords = gtid.xy + TSF_FILTER_WIDTH;

    if (any(coords >= resolution))
        return;

    float shadowFiltered = 0;
    float centerShadow = ldsShadow[ldsCoords.y][ldsCoords.x];

#if defined(TSF_FILTER_WIDTH) && TSF_FILTER_WIDTH > 0
    static const uint Weights = TSF_FILTER_WIDTH - 1;

    float totalWeight = 0;

    for (int dy = -TSF_FILTER_WIDTH; dy <= TSF_FILTER_WIDTH; ++dy)
    {
        int ay = abs(dy);
        float wy = GaussianFilterWeights[Weights][ay];

        for (int dx = -TSF_FILTER_WIDTH; dx <= TSF_FILTER_WIDTH; ++dx)
        {
            int ax = abs(dx);
            float wx = GaussianFilterWeights[Weights][ax];

            float w = wx * wy;
            float s = ldsShadow[ldsCoords.y + dy][ldsCoords.x + dx];

            shadowFiltered += w * s;
            totalWeight    += w;
        }
    }

    shadowFiltered /= totalWeight;

#else
    shadowFiltered = centerShadow;
#endif

    shadowTerm[coords] = shadowFiltered;
}
#endif
