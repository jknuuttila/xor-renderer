#include "VisualizeTriangulation.sig.h"

struct PSInput
{
    float4 uvHeight : TEXCOORD0;
    float4 areaUv   : TEXCOORD1;
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

	float4 color = 0;
#if defined(SHOW_ERROR)
	color.rgb = signedColor(approxHeight - height, maxError);
#elif defined(CPU_ERROR)
    //color.rgb = signedColor(cpuCalculatedError.Sample(pointSampler, i.areaUv.xy), maxError);
    //color.rgb = signedColor(, maxError);
	color.r = remap(minHeight, maxHeight, 0, 1, cpuCalculatedError.Sample(pointSampler, i.areaUv.xy));
#else
	color.r = v;
#endif
	color.a      = 1;

	return color;
#endif
}
