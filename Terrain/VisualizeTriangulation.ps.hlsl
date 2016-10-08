#include "VisualizeTriangulation.sig.h"

struct PSInput
{
    float4 uv : TEXCOORD0;
};

[RootSignature(VisualizeTriangulation_ROOT_SIGNATURE)]
float4 main(PSInput i) : SV_Target
{
#ifdef WIREFRAME
	return 1;
#else
	float height = heightMap.Sample(pointSampler,  i.uv.xy);
	float v      = remap(minHeight, maxHeight, 0, 1, height);

	float4 color = v;
	color.gb     = 0;
	color.a      = 1;

	return color;
#endif
}
