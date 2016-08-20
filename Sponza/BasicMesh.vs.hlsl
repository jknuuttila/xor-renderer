#include "BasicMesh.sig.h"

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

[RootSignature(BASICMESH_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;

    o.uv    = float4(i.uv.xy, 0, 0);
    o.pos   = mul(modelViewProj, float4(i.pos, 1));

	return o;
}
