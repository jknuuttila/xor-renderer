#ifndef VisualizeTriangulation_SIG_H
#define VisualizeTriangulation_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(VisualizeTriangulation)

XOR_CBUFFER(Constants, 0)
{
	float2 minCorner;
	float2 maxCorner;
	float minHeight;
	float maxHeight;
	float maxError;
};

XOR_SRV(Texture2D<float>, heightMap, 0)

XOR_SAMPLER_POINT(pointSampler)

XOR_END_SIGNATURE

#define VisualizeTriangulation_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(1,1)

#endif
