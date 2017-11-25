#include "RenderTerrain.sig.h"

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 prevPos  : POSITION1;
    float4 uv       : TEXCOORD0;
    float4 svPos    : SV_Position;
};

struct PSOutput
{
    float4 color      : SV_Target0;
};

[RootSignature(RENDERTERRAIN_ROOT_SIGNATURE)]
PSOutput main(PSInput i)
{
    PSOutput o;

    float2 uv       = i.uv.xy;
    float2 noiseUV  = i.svPos.xy / noiseResolution;
    float2 screenUV = i.svPos.xy / resolution;

    float4 reprojectedPos = mul(prevViewProj, float4(i.worldPos.xyz, 1));
    float2 reprojectedUV = ndcToUV(reprojectedPos.xy / reprojectedPos.w);

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
    float shadowZ            = terrainShadows.Sample(pointSampler, shadowUV + shadowNoiseOffset);

    // reprojectedUV = screenUV;
    // float reprojectedShadow = shadowHistory.Sample(bilinearSampler, reprojectedUV);
    // float shadow = shadowPos.z >= shadowZ ? 1 : 0;
    // shadowUV += shadowNoiseOffset;
    // float shadow = terrainShadows.SampleCmp(pcfSampler, shadowUV + shadowNoiseOffset, shadowPos.z + .01);
    // // float shadow = sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV, shadowPos.z + .01, shadowResolution);
    // // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(1, 0)  / shadowResolution, shadowPos.z + .01, shadowResolution);
    // // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(-1, 0) / shadowResolution, shadowPos.z + .01, shadowResolution);
    // // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(0, 1)  / shadowResolution, shadowPos.z + .01, shadowResolution);
    // // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(0, -1) / shadowResolution, shadowPos.z + .01, shadowResolution);
    // // shadow /= 5;

    // float2 shadowUnnormalized = shadowUV * shadowResolution;
    // float2 shadowFrac = frac(shadowUnnormalized);
    // float shadowConfidence = 1 - max(abs(shadowFrac.x - 0.5), abs(shadowFrac.y - 0.5)) * 2;

    // float k = 1 - saturate(pow(saturate(shadowConfidence), saturate(shadowHistoryBlend) * 20));

    // if (shadow == 0) k = 0;
    // if (shadow == 1) k = 0;

    // shadow = lerp(shadow, reprojectedShadow, k);

    float shadow = shadowTerm.Sample(pointSampler, screenUV);
    // shadow = k;
    //shadow = lerp(shadow, reprojectedShadow, shadowHistoryBlend);

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
#elif defined(HIGHLIGHT_CRACKS)
    o.color.rgb = h * .05;
#else
    o.color.rgb = albedo;
#endif
    o.color.a   = 1;

    return o;
}
