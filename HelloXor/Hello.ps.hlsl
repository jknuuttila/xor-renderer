#include "Xor/Shaders.h.hlsl"

[RootSignature(XOR_ROOT_SIGNATURE)]
float4 main(float4 color : COLOR0) : SV_Target
{
	return color;
}
