#include "RenderTerrain.sig.h"

struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 prevPos  : POSITION1;
    float4 uv       : TEXCOORD0;
    float4 pos      : SV_Position;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    TerrainVertex v = makeTerrainVertex(i);

    VSOutput o;
	o.worldPos.xz = v.worldCoords();
    o.worldPos.y  = v.loddedHeight;
    o.worldPos.w  = 1;
    o.uv          = float4(v.uv(), v.normalizedPos());
	o.pos         = mul(viewProj, o.worldPos);
    o.prevPos     = mul(prevViewProj, o.worldPos);
    o.prevPos.xyz /= o.prevPos.w;

    if (v.isCulled())
        o.pos.xyz = NaN;

    return o;
}
