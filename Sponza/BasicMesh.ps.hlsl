#include "BasicMesh.sig.h"

struct PSInput
{
    float4 uv     : TEXCOORD0;
    float4 normal : NORMAL0;
};

[RootSignature(BASICMESH_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    float3 albedo  = albedoTex.Sample(bilinear, i.uv.xy).rgb;


    float cosineTerm = max(0, dot(i.normal.xyz, sunDirection.xyz));
    float3 diffuse   = 1 / Pi * sunColor.rgb * cosineTerm;
    float3 color     = albedo * (ambientColor.rgb + diffuse);

    return float4(color, 1);
}
