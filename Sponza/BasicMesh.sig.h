#ifndef BASICMESH_SIG_H
#define BASICMESH_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(BasicMesh)


XOR_CBUFFER(Constants, 0)
{
    float4x4 modelViewProj;

    float4 sunDirection;
    float4 sunColor;

    float4 ambientColor;
};

XOR_SRV(Texture2D<float4>, albedoTex, 0)

XOR_SAMPLER_BILINEAR(bilinear)


XOR_END_SIGNATURE

#define BASICMESH_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(1, 1)

#endif
