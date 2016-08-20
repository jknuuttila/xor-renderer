#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(BasicMesh)


XOR_CBUFFER(Constants, 0)
{
    float4x4 modelViewProj;
};

XOR_SRV(Texture2D<float4>, diffuseTex, 0)

XOR_SAMPLER_BILINEAR(bilinear)


XOR_END_SIGNATURE

#define BASICMESH_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(1, 1)
