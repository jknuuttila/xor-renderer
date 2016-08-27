#include "ImguiRenderer.sig.h"

struct VSInput
{
    float2 pos   : POSITION0;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 uv    : TEXCOORD0;
    float4 color : COLOR0;
    float4 pos   : SV_Position;
};

[RootSignature(IMGUIRENDERER_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;

    float2 fracPos = i.pos * reciprocalResolution;
    float2 clipPos = lerp(float2(-1, 1), float2(1, -1), fracPos);

    o.uv    = float4(i.uv.xy, 0, 0);
    o.color = i.color;
    o.pos   = float4(clipPos, 0, 1);

	return o;
}
