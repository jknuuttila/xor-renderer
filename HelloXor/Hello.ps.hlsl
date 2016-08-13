#include "Xor/Shaders.h.hlsl"

Texture2D<float4> tex : register(t0);
SamplerState bilinear : register(s0);

struct PSInput
{
    float4 color : COLOR0;
    float4 uv : TEXCOORD0;
};

[RootSignature(XOR_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    return tex.Sample(bilinear, i.uv.xy);
}
