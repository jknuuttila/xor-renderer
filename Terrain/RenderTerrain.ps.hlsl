#include "RenderTerrain.sig.h"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 uv       : TEXCOORD0;
    float4 svPos    : SV_Position;
};

struct PSOutput
{
    float4 color      : SV_Target0;
    float  shadowTerm : SV_Target1;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
PSOutput main(PSInput i)
{
    PSOutput o;

    float2 uv       = i.uv.xy;
    float2 noiseUV  = i.svPos.xy / noiseResolution;
    float2 screenUV = i.svPos.xy / resolution;

    float h = (i.worldPos.y - heightMin) / (heightMax - heightMin);
#ifdef WIREFRAME
    float3 albedo = 1;
#elif defined(TEXTURED)
    float3 albedo = terrainColor.Sample(bilinearSampler, uv).rgb;
#else
    float3 albedo = h;
#endif

    float4 noise = noiseTexture.Sample(pointWrapSampler, noiseUV);

    float ambientOcclusion = terrainAO.Sample(bilinearSampler, i.uv.zw);

    float4 shadowPos         = mul(shadowViewProj, i.worldPos);
    shadowPos.xyz           /= shadowPos.w;
    float2 shadowUV          = ndcToUV(shadowPos.xy);
    float2 shadowNoiseOffset = lerp(-1, 1, noise.xy) * noiseAmplitude;
    float2 shadowNoiseOffset2 = lerp(-1, 1, noise.zw) * noiseAmplitude;
    float shadowZ            = terrainShadows.Sample(pointSampler, shadowUV + shadowNoiseOffset);

    float reprojectedShadow = shadowHistory.Sample(bilinearSampler, screenUV);
    // float shadow = shadowPos.z >= shadowZ ? 1 : 0;
    //float shadow = terrainShadows.SampleCmp(pcfSampler, shadowUV + shadowNoiseOffset, shadowPos.z);
    shadowUV += shadowNoiseOffset;
    float shadow = sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV, shadowPos.z, shadowResolution);
    shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(1, 0) / shadowResolution, shadowPos.z, shadowResolution);
    shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(-1, 0) / shadowResolution, shadowPos.z, shadowResolution);
    shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(0, 1) / shadowResolution, shadowPos.z, shadowResolution);
    shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(0, -1) / shadowResolution, shadowPos.z, shadowResolution);
    shadow /= 5;

    float2 shadowUnnormalized = shadowUV * shadowResolution;
    float2 shadowFrac = frac(shadowUnnormalized);
    float shadowConfidence = 1 - max(abs(shadowFrac.x - 0.5), abs(shadowFrac.y - 0.5)) * 2;
    float confExp = 0.3;

    float k = 1 - pow(shadowConfidence, shadowHistoryBlend * 10);
    shadow = lerp(shadow, reprojectedShadow, k);
    // shadow = k;
    //shadow = lerp(shadow, reprojectedShadow, shadowHistoryBlend);

    o.shadowTerm = shadow;

#if defined(LIGHTING)
    float3 N = normalize(terrainNormal.Sample(bilinearSampler, uv).xyz);
    N = N.xzy;

    float3 L = sunDirection.xyz;
    // float3 color = saturate(dot(N, L)) / Pi * sunColor.rgb * albedo;
    float3 color = saturate(dot(N, L)) / Pi * sunColor.rgb;
    color *= shadow;
    color += ambientOcclusion * ambient.rgb;

    o.color.rgb = color;
#elif defined(SHOW_AO)
    o.color.rgb = ambientOcclusion;
#elif defined(SHADOW_TERM)
    o.color.rgb = lerp(0.1, 0.9, shadow) * ambientOcclusion;
#elif defined(SHADOW_HISTORY)
    o.color.rgb = o.shadowTerm;
#else
    o.color.rgb = albedo;
#endif
    o.color.a   = 1;

    return o;
}
