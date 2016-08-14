#include "Hello.sig.h"

struct PSInput
{
    float4 color : COLOR0;
    float4 uv : TEXCOORD0;
};

[RootSignature(HELLO_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    return tex.Sample(bilinear, i.uv.xy);
}
