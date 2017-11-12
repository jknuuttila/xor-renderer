#include "VisualizeTriangulation.sig.h"

struct VSInput
{
	int2  pixelCoords : POSITION0;
	float height      : POSITION1;
};

struct VSOutput
{
	float4 uvHeight : TEXCOORD0;
    float4 areaUv   : TEXCOORD1;
    float4 pos      : SV_Position;
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    float2 worldPos      = terrainWorldCoords(i.pixelCoords);
    float2 uv            = terrainUV(i.pixelCoords);
    float2 normalizedPos = terrainNormalizedPos(i.pixelCoords);

    VSOutput o;
	o.uvHeight = float4(uv, i.height, 0);
    o.areaUv   = float4(normalizedPos, 0, 0);
	o.pos      = float4(lerp(minCorner, maxCorner, normalizedPos), 0, 1);
    return o;
}
