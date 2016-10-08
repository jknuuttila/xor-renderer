#include "RenderTerrain.sig.h"

struct VSInput
{
    float2 normalizedPos : POSITION0;
    float  height        : POSITION1;
};

struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 pos      : SV_Position;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;
	o.worldPos.xz = lerp(worldMin, worldMax, i.normalizedPos);
    o.worldPos.y  = i.height;
    o.worldPos.w  = 1;
	o.pos =  mul(viewProj, o.worldPos);
    return o;
}
