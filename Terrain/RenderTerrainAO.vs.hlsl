#include "RenderTerrainAO.sig.h"

struct VSOutput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 pos      : SV_Position;
};

[RootSignature(RENDERTERRAINAO_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    TerrainVertex v = makeTerrainVertex(i);

    VSOutput o;
	o.worldPos.xz = v.worldCoords();
    o.worldPos.y  = v.loddedHeight;
    o.worldPos.w  = 1;
    o.uv          = float4(v.normalizedPos(), 0, 0);
	o.pos         = mul(viewProj, o.worldPos);
    return o;
}
