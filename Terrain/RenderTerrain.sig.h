#ifndef RENDERTERRAIN_SIG_H
#define RENDERTERRAIN_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(RenderTerrain)


XOR_CBUFFER(Constants, 0)
{
    float4x4 viewProj;
	float2 worldMin;
	float2 worldMax;
    float heightMin;
    float heightMax;
};

XOR_END_SIGNATURE

#define RENDERTERRAIN_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_C(1)

#endif

