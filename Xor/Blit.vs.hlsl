#include "Xor/Blit.sig.h"

struct VSOutput
{
    float4 uv  : TEXCOORD0;
    float4 pos : SV_Position;
};

static const float2 VertexUV[6] =
{
    float2(0, 0),
    float2(0, 1),
    float2(1, 0),
    float2(0, 1),
    float2(1, 1),
    float2(1, 0),
};

[RootSignature(BLIT_ROOT_SIGNATURE)]
VSOutput main(uint id : SV_VertexID)
{
    VSOutput o;

    float2 uv = VertexUV[min(id, 5)];
    o.uv  = float4(lerp( uvBegin,  uvEnd, uv), 0, 0);
    o.pos = float4(lerp(posBegin, posEnd, uv), 0, 1);

	return o;
}
