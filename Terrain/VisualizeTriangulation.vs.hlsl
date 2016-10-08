#include "VisualizeTriangulation.sig.h"

struct VSInput
{
	float2 normalizedPos : POSITION0;
	float2 uv            : TEXCOORD;
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
	o.uv  = float4(i.uv, 0, 0);
	o.pos = float4(lerp(minCorner, maxCorner, i.normalizedPos), 0, 1);
    return o;
}
