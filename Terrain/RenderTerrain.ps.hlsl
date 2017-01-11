#include "RenderTerrain.sig.h"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 svPos    : SV_Position;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
    float2 uv = i.uv.xy;

    float h = (i.worldPos.y - heightMin) / (heightMax - heightMin);
#ifdef WIREFRAME
    float3 albedo = 1;
#elif defined(TEXTURED)
    float3 albedo = terrainColor.Sample(bilinearSampler, uv).rgb;
#else
    float3 albedo = h;
#endif

    float ambientOcclusion = terrainAO.Sample(bilinearSampler, i.uv.zw);

#if defined(LIGHTING)
    float3 N = normalize(terrainNormal.Sample(bilinearSampler, uv).xyz);
    N = N.xzy;

    float4 shadowPos = mul(shadowViewProj, i.worldPos);
    shadowPos.xyz   /= shadowPos.w;
    float2 shadowUV  = ndcToUV(shadowPos.xy);
    float shadowZ    = terrainShadows.Sample(pointSampler, shadowUV);

    float shadow     = shadowPos.z >= shadowZ ? 1 : 0;

    float3 L = sunDirection.xyz;
    // float3 color = saturate(dot(N, L)) / Pi * sunColor.rgb * albedo;
    float3 color = saturate(dot(N, L)) / Pi * sunColor.rgb;
    color *= shadow;

	return float4(color, 1);
#elif defined(SHOW_AO)
    float3 color = ambientOcclusion;
	return float4(color, 1);
#else
	return float4(albedo, 1);
#endif
}
