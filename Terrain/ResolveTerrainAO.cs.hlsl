#include "ResolveTerrainAO.sig.h"

[RootSignature(RESOLVETERRAINAO_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 id = tid.xy;

    if (any(id >= size))
        return;

    uint visibleSamples = terrainAOVisibleSamples[id];
    float visibility    = float(visibleSamples) / maxVisibleSamples;
    terrainAO[id]       = visibility;
}
