#include "Xor/Blit.sig.h"

float4 main(float4 uv : TEXCOORD0) : SV_Target
{
    float4 color = src.SampleLevel(pointSampler, uv.xy, mip);
    color = color * multiplier + bias;
    color.rgb *= color.a;
    return color;
}
