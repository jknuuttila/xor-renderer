#include "ResolveTerrainAO.sig.h"

float loadSample(int2 pos)
{
    pos = clamp(pos, 0, size - 1);
    return float(terrainAOVisibleSamples[pos]);
}

[RootSignature(RESOLVETERRAINAO_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid : SV_DispatchThreadID)
{
    int2 id = tid.xy;

    if (any(id >= size))
        return;

    float visibility = 0;

    if (blurKernelSize <= 0)
    {
        visibility = loadSample(id);
    }
    else
    {
        for (int y = -blurKernelSize; y <= blurKernelSize; ++y)
        {
            float rowSum = 0;

            for (int x = -blurKernelSize; x <= blurKernelSize; ++x)
            {
                float xw = blurWeights[abs(x)].x;
                rowSum += xw * loadSample(id + int2(x, y));
            }

            float yw = blurWeights[abs(y)].x;
            visibility += yw * rowSum;
        }
    }

    visibility /= maxVisibleSamples;
    terrainAO[id] = visibility;
}
