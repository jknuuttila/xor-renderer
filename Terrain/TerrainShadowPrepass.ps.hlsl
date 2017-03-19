#include "RenderTerrain.sig.h"

#ifndef TSP_NOISE_SAMPLES
#define TSP_NOISE_SAMPLES 0
#endif

struct PSInput
{
    float4 worldPos : POSITION0;
    float4 prevPos  : POSITION1;
    float4 uv       : TEXCOORD0;
    float4 svPos    : SV_Position;
};

struct PSOutput
{
    float  shadowTerm   : SV_Target0;
    float2 motionVector : SV_Target1;
};

static const float2 noiseOffsets[4] =
{
    float2(0, 0),
    float2(0.5, 0),
    float2(0, 0.5),
    float2(0.5, 0.5),
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

    float4 shadowPos         = mul(shadowViewProj, i.worldPos);
    shadowPos.xyz           /= shadowPos.w;
    float2 shadowUV          = ndcToUV(shadowPos.xy);

    float comparisonValue = shadowPos.z + shadowBias;

    float shadow = 0;
    if (TSP_NOISE_SAMPLES == 0)
        shadow = terrainShadows.SampleCmp(pcfSampler, shadowUV, comparisonValue);

    [unroll]
    for (uint i = 0; i < TSP_NOISE_SAMPLES; i += 2)
    {
        float4 noise = noiseTexture.Sample(pointWrapSampler, noiseUV + noiseOffsets[i / 2]);
        noise = lerp(-1, 1, noise) * noiseAmplitude;

        float2 evenNoise = noise.xy;
        float2 oddNoise  = noise.zw;

        float evenShadow = terrainShadows.SampleCmp(pcfSampler, shadowUV + evenNoise, comparisonValue);
        shadow += evenShadow;

        if (i < (TSP_NOISE_SAMPLES - 1))
        {
            float oddShadow = terrainShadows.SampleCmp(pcfSampler, shadowUV + oddNoise, comparisonValue);
            shadow += oddShadow;
        }
    }

    //shadow /= (1 + TSP_NOISE_SAMPLES);
    shadow /= max(1, TSP_NOISE_SAMPLES);

    // float shadow = sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV, shadowPos.z + .01, shadowResolution);
    // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(1, 0)  / shadowResolution, shadowPos.z + .01, shadowResolution);
    // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(-1, 0) / shadowResolution, shadowPos.z + .01, shadowResolution);
    // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(0, 1)  / shadowResolution, shadowPos.z + .01, shadowResolution);
    // shadow += sampleCmpBicubicBSpline(terrainShadows, pcfSampler, shadowUV + shadowNoiseOffset + float2(0, -1) / shadowResolution, shadowPos.z + .01, shadowResolution);
    // shadow /= 5;

    o.shadowTerm   = shadow;
    o.motionVector = reprojectedUV - screenUV; 

    return o;
}
