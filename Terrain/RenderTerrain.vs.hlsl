#include "RenderTerrain.sig.h"

struct VSInput
{
    int2  pixelCoords : POSITION0;
    float height      : POSITION1;
};

struct VSOutput
{
    float4 worldPos     : POSITION0;
    float4 prevPos      : POSITION1;
    float4 uv           : TEXCOORD0;
    float4 pos          : SV_Position;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    float2 worldPos      = terrainWorldCoords(i.pixelCoords);
    float2 uv            = terrainUV(i.pixelCoords);
    float2 normalizedPos = terrainNormalizedPos(i.pixelCoords);

    VSOutput o;
	o.worldPos.xz = worldPos;
    o.worldPos.y  = i.height;
    o.worldPos.w  = 1;
    o.uv          = float4(uv, normalizedPos);
	o.pos         = mul(viewProj, o.worldPos);
    o.prevPos     = mul(prevViewProj, o.worldPos);
    o.prevPos.xyz /= o.prevPos.w;
    return o;
}
