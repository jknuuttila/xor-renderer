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

#if defined(LIGHTING)
    float3 N = normalize(terrainNormal.Sample(bilinearSampler, uv).xyz);
	// return float4(N, 1);
    N = N.xzy;

    float3 L = sunDirection.xyz;
    float3 color = saturate(dot(N, L)) / Pi * sunColor.rgb;

	return float4(color, 1);
#else
	return float4(albedo, 1);
#endif
}
