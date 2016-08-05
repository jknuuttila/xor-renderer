#include "Xor/Shaders.h.hlsl"

struct PSInput
{
    float4 color : COLOR0;
    float4 uv : TEXCOORD0;
};

[RootSignature(XOR_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
#if 0
    return float4(i.uv.x, 0, i.uv.y, 1);
#else
	return i.color;
#endif
}
