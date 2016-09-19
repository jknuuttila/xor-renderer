#include "RenderTerrain.sig.h"

struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 pos      : SV_Position;
};
[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
VSOutput main(float3 pos : POSITION)
{
    VSOutput o;
    o.worldPos = float4(pos, 1);
	o.pos =  mul(viewProj, o.worldPos);
    return o;
}
