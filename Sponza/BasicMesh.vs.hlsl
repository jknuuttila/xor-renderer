#include "BasicMesh.sig.h"

struct VSInput
{
    float3 pos    : POSITION0;
    float3 uv     : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VSOutput
{
    float4 uv            : TEXCOORD0;
    float4 worldPosition : POSITION0;
    float4 worldNormal   : NORMAL0;
    float4 pos           : SV_Position;
};

[RootSignature(BASICMESH_ROOT_SIGNATURE)]
VSOutput main(VSInput i)
{
    VSOutput o;

    o.uv            = float4(i.uv.xy, 0, 0);
    o.worldPosition = float4(i.pos, 1);
    o.worldNormal   = float4(i.normal, 0);
    o.pos           = mul(modelViewProj, float4(i.pos, 1));

	return o;
}
