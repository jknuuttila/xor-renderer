#include "VisualizeTriangulation.sig.h"

struct VSInput
{
	float3 pos : POSITION;
};

struct VSOutput
{
	float4 uv  : TEXCOORD0;
    float4 pos : SV_Position;
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;
	o.uv  = float4(i.pos.xy, 0, 0);
	o.pos = float4(lerp(minCorner, maxCorner, o.uv.xy), 0, 1);
    return o;
}
