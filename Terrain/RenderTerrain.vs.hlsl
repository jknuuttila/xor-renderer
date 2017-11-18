#include "RenderTerrain.sig.h"

struct VSInput
{
    int2  pixelCoords        : POSITION0;
    float height             : POSITION1;
    int2  nextLodPixelCoords : POSITION2;
    float nextLodHeight      : POSITION3;
};

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
    TerrainVertex v = makeTerrainVertex(i.pixelCoords, i.height,
                                        i.nextLodPixelCoords, i.nextLodHeight);

    VSOutput o;
	o.worldPos.xz = v.worldCoords();
    o.worldPos.y  = v.height;
    o.worldPos.w  = 1;
    o.uv          = float4(v.uv(), v.normalizedPos());
	o.pos         = mul(viewProj, o.worldPos);
    o.prevPos     = mul(prevViewProj, o.worldPos);
    o.prevPos.xyz /= o.prevPos.w;
    return o;
}
