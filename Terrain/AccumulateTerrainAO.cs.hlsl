#include "AccumulateTerrainAO.sig.h"

[RootSignature(ACCUMULATETERRAINAO_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 id = tid.xy;

    if (any(id >= size))
        return;

    uint visibleBits            = terrainAOVisibleBits[id];
    uint visibleSamples         = terrainAOVisibleSamples[id];
    visibleSamples             += countbits(visibleBits);
    terrainAOVisibleBits[id]    = 0;
    terrainAOVisibleSamples[id] = visibleSamples;
}
