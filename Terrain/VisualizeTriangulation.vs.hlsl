#include "VisualizeTriangulation.sig.h"

struct VSInput
{
	float2 normalizedPos : POSITION0;
	float  height        : POSITION1;
	float2 uv            : TEXCOORD;
};

struct VSOutput
{
	float4 uvHeight : TEXCOORD0;
    float4 pos      : SV_Position;
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;
	o.uvHeight = float4(i.uv, i.height, 0);
	o.pos      = float4(lerp(minCorner, maxCorner, i.normalizedPos), 0, 1);
    return o;
}
