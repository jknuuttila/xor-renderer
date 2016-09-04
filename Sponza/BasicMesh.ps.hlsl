#include "BasicMesh.sig.h"

struct PSInput
{
    float4 uv            : TEXCOORD0;
    float4 worldPosition : POSITION0;
    float4 worldNormal   : NORMAL0;
};

[RootSignature(BASICMESH_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    float3 N = i.worldNormal.xyz;
    float3 L = sunDirection.xyz;
    float3 V = normalize(cameraPosition.xyz - i.worldPosition.xyz);

    float3 albedo  = albedoTex.Sample(bilinear, i.uv.xy).rgb;

    float3 diffuseAlbedo  = albedo;
    float  specularAlbedo = 1;

    float cosineTerm = nonNegative(dot(N, L));
    float3 diffuse   = 1 / Pi * diffuseAlbedo * cosineTerm;
    float specular   = GGX(N, V, L, materialProperties.roughness, materialProperties.F0) * specularAlbedo * cosineTerm;

    float3 ambient   = albedo * ambientColor.rgb;
    float3 color     = ambient + (diffuse + specular) * sunColor.rgb;

    return float4(color, 1);
}
