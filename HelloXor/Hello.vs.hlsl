#include "Hello.sig.h"

struct VSInput
{
    float3 pos   : POSITION0;
    float3 uv    : TEXCOORD0;
};

struct VSOutput
{
    float4 uv    : TEXCOORD0;
    float4 pos   : SV_Position;
};

[RootSignature(HELLO_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;

    o.uv    = float4(i.uv.xy, 0, 0);
    o.pos   = mul(viewProj,
                  mul(model, float4(i.pos, 1)));

	return o;
}
