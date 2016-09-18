#include "Xor/Blit.sig.h"

float4 main(float4 uv : TEXCOORD0) : SV_Target
{
    float4 color = src.SampleLevel(pointSampler, uv.xy, 0);

    color = color * multiplier + bias;
    color.gb = 0;
    color.a = 1;

    return color;
}
