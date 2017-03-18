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
    float  shadowTerm   : SV_Target0;
    float2 motionVector : SV_Target1;
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

    float4 noise = noiseTexture.Sample(pointWrapSampler, noiseUV);

    float4 shadowPos         = mul(shadowViewProj, i.worldPos);
    shadowPos.xyz           /= shadowPos.w;
    float2 shadowUV          = ndcToUV(shadowPos.xy);
    float2 shadowNoiseOffset = lerp(-1, 1, noise.xy) * noiseAmplitude;
    float shadowZ            = terrainShadows.Sample(pointSampler, shadowUV + shadowNoiseOffset);
    shadowUV += shadowNoiseOffset;

    float shadow = terrainShadows.SampleCmp(pcfSampler, shadowUV + shadowNoiseOffset, shadowPos.z + .01);
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
