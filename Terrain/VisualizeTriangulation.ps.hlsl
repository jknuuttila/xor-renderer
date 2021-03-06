#include "VisualizeTriangulation.sig.h"

struct PSInput
{
    float4 uvHeight          : TEXCOORD0;
    float4 areaUvWorldCoords : TEXCOORD1;
    float4 svPosition : SV_Position;
};

static const float3 LodColors[10] =
{
    float3(1, 0, 0),
    float3(0, 1, 0),
    float3(0, 0, 1),
    float3(1, 1, 0),
    float3(1, 0, 1),
    float3(0, 1, 1),
    float3(0.5, 0.5, 0),
    float3(0.5, 0, 0.5),
    float3(0, 0.5, 0.5),
    float3(1, 1, 1),
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
#ifdef WIREFRAME
	return 1;
#else
	float approxHeight = i.uvHeight.z;

	float height = heightMap.Sample(pointSampler, i.uvHeight.xy);
	//float v      = remap(minHeight, maxHeight, 0, 1, height);
	float v      = remap(minHeight, maxHeight, 0, 1, approxHeight);

    float2 uv          = i.areaUvWorldCoords.xy;
    float2 worldCoords = i.areaUvWorldCoords.zw;

    float distanceToCamera = length(worldCoords - cameraWorldCoords);
    bool cameraNear = distanceToCamera < 50;

	float4 color = 0;
#if defined(SHOW_ERROR)
	color.rgb = signedColor(approxHeight - height, maxError);
#elif defined(CPU_ERROR)
    color.rgb = signedColor(cpuCalculatedError.Sample(pointSampler, uv), maxError);
#elif defined(LOD_LEVEL)
    if (cameraNear)
        color.rgb = 1;
    else
        color.rgb = LodColors[clamp(lodLevel, 0, 9)];
#elif defined(CONTINUOUS_LOD)
    float lod = terrainLOD(distanceToCamera);

    lod = clamp(lod, 0, 9);

    float3 A = LodColors[uint(floor(lod))];
    float3 B = LodColors[uint(ceil(lod))];

    if (cameraNear)
        color.rgb = 1;
    else
        color.rgb = lerp(A, B, frac(lod));
#elif defined(CLUSTER_ID)
    if (cameraNear)
        color.rgb = 1;
    else
        color.rgb = randomColorFromUint(clusterId);
#else
    if (cameraNear)
        color.rgb = v + .25;
    else
        color.r = v;
#endif
	color.a = 1;

	return color;
#endif
}
