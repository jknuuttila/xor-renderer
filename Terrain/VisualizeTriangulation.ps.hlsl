#include "VisualizeTriangulation.sig.h"

struct PSInput
{
    float4 uvHeight : TEXCOORD0;
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
#ifdef WIREFRAME
	return 1;
#else
	float approxHeight = i.uvHeight.z;

	float height = heightMap.Sample(pointSampler, i.uvHeight.xy);
	float v      = remap(minHeight, maxHeight, 0, 1, height);

	float4 color = 0;
#ifdef SHOW_ERROR
	color.rgb = signedColor(approxHeight - height, maxError);
#else
	color.r = v;
#endif
	color.a      = 1;

	return color;
#endif
}
