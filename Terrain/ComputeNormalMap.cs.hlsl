#include "ComputeNormalMap.sig.h"

float loadHeight(uint2 pos)
{
    pos = clamp(pos, uint2(0, 0), uint2(size - 1));
    return heightMap.Load(uint3(pos, 0)) * heightMultiplier;
}

[RootSignature(ComputeNormalMap_ROOT_SIGNATURE)]
[numthreads(XOR_NUMTHREADS)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 pos = tid.xy;

#if 0
    uint2 sz = 256;
    
    if (pos.x == 0 && pos.y == 0)
        debugPrint3(size, axisMultiplier, heightMultiplier);
#endif

    if (any(pos >= size))
        return;

    float n = loadHeight(pos - uint2(0, 1));
    float s = loadHeight(pos + uint2(0, 1));
    float w = loadHeight(pos - uint2(1, 0));
    float e = loadHeight(pos + uint2(1, 0));

    float2 xy = float2(2, 2) * axisMultiplier;

    float3 tangent   = float3(xy.x,    0, e - w);
    float3 bitangent = float3(   0, xy.y, n - s);
    float3 normal    = normalize(cross(tangent, bitangent));

    normalMap[pos] = float4(normal, 0);
}
