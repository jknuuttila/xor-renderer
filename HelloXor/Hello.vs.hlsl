#include "Xor/Shaders.h.hlsl"

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

struct VSOutput
{
    float4 color : COLOR0;
    float4 pos   : SV_Position;
};

[RootSignature(XOR_ROOT_SIGNATURE)]
VSOutput main(uint id : SV_VertexID)
{
    id %= 3;

    VSOutput o;
    o.color = Colors[id];
    o.pos   = Positions[id];

	return o;
}