#include "VisualizeTriangulation.sig.h"

struct VSInput
{
    int2  pixelCoords        : POSITION0;
    float height             : POSITION1;
    int2  nextLodPixelCoords : POSITION2;
    float nextLodHeight      : POSITION3;
};

struct VSOutput
{
	float4 uvHeight          : TEXCOORD0;
    float4 areaUvWorldCoords : TEXCOORD1;
    float4 pos               : SV_Position;
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    TerrainVertex v = makeTerrainVertex(i.pixelCoords, i.height,
                                        i.nextLodPixelCoords, i.nextLodHeight);

    VSOutput o;
	o.uvHeight          = float4(v.uv(), v.loddedHeight, 0);
    o.areaUvWorldCoords = float4(v.normalizedPos(), v.worldCoords());
	o.pos               = float4(lerp(minCorner, maxCorner, v.normalizedPos()), 0, 1);
    return o;
}
