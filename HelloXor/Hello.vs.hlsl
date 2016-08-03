#include "Xor/Shaders.h.hlsl"

#if 0
static const float P = .75f;

static const float4 Positions[] =
{
    float4(-P, -P, 0, 1),
    float4( P, -P, 0, 1),
    float4( 0,  P, 0, 1),
};

static const float4 Colors[] =
{
    float4(1, 0, 0, 1),
    float4(0, 1, 0, 1),
    float4(0, 0, 1, 1),
};
#endif

struct VSInput
{
    float2 pos   : POSITION0;
    float2 uv    : TEXCOORD0;
    float3 color : COLOR0;
};

struct VSOutput
{
    float4 color : COLOR0;
    float4 pos   : SV_Position;
};

[RootSignature(XOR_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;

    o.color = float4(i.color, 1);
    o.pos   = float4(i.pos, 0, 1);

	return o;
}
