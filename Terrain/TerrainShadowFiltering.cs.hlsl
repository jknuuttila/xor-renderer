#include "TerrainShadowFiltering.sig.h"

#ifndef TSF_FILTER_WIDTH
#define TSF_FILTER_WIDTH 1
#endif

static const float GaussianFilterWeights[3][4] =
{
    2,  1, 0, 0,
    6,  4, 1, 0,
    20, 15, 6, 1,
};

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
