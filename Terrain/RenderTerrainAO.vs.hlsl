#include "RenderTerrainAO.sig.h"

struct VSInput
{
    float2 normalizedPos : POSITION0;
    float  height        : POSITION1;
    float2 uv            : TEXCOORD0;
};

struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 pos      : SV_Position;
};

[RootSignature(RENDERTERRAINAO_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;
	o.worldPos.xz = lerp(worldMin, worldMax, i.normalizedPos);
    o.worldPos.y  = i.height;
    o.worldPos.w  = 1;
    //o.uv          = float4(i.uv, 0, 0);
    o.uv          = float4(i.normalizedPos, 0, 0);
	o.pos         = mul(viewProj, o.worldPos);
    return o;
}
