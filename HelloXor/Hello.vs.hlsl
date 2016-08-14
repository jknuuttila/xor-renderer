#include "Xor/Shaders.h.hlsl"

cbuffer OffsetConstants : register(b0)
{
    float2 offset;
};
cbuffer SizeConstants : register(b1)
{
    float2 size;
};

struct VSInput
{
    float2 pos   : POSITION0;
    float2 uv    : TEXCOORD0;
    float3 color : COLOR0;
};

struct VSOutput
{
    float4 color : COLOR0;
    float4 uv    : TEXCOORD0;
    float4 pos   : SV_Position;
};

[RootSignature(XOR_ROOT_SIGNATURE_CS(2, 1))]
VSOutput main(VSInput i)
{
    VSOutput o;

    o.color = float4(i.color, 1);
    o.uv    = float4(i.uv, 0, 0);
    o.pos   = float4(i.pos * size + offset, 0, 1);

	return o;
}
