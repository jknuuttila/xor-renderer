#include "RenderTerrainAO.sig.h"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
};

[RootSignature(RENDERTERRAINAO_ROOT_SIGNATURE)]
[earlydepthstencil]
void main(PSInput i)
{
    float2 coords  = i.uv.xy * aoTextureSize;
    uint2  iCoords = uint2(coords);
    InterlockedOr(terrainAOVisibleBits[iCoords], aoBitMask);
}
