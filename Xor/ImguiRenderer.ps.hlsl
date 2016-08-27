#include "ImguiRenderer.sig.h"

struct PSInput
{
    float4 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

[RootSignature(IMGUIRENDERER_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    float4 texColor  = tex.Sample(pointSampler, i.uv.xy);
    float4 vertColor = i.color;
    return texColor * vertColor;
}
