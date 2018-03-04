#include "VisualizeTriangulation.sig.h"

struct VSOutput
{
	float4 uvHeight          : TEXCOORD0;
    float4 areaUvWorldCoords : TEXCOORD1;
    float4 pos               : SV_Position;
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    TerrainVertex v = makeTerrainVertex(i);

    VSOutput o;
	o.uvHeight          = float4(v.uv(), v.loddedHeight, 0);
    o.areaUvWorldCoords = float4(v.normalizedPos(), v.worldCoords());
	o.pos               = float4(lerp(minCorner, maxCorner, v.normalizedPos()), 0, 1);

    if (v.isCulled())
        o.pos.xyz = NaN;

    return o;
}
