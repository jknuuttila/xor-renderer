#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(Hello)


XOR_CBUFFER(Constants, 0)
{
    float4x4 viewProj;
    float4x4 model;
};

XOR_TEXTURE_SRV(Texture2D<float4>, tex, 0)

XOR_SAMPLER_BILINEAR(bilinear)


XOR_END_SIGNATURE

#define HELLO_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(1, 1)
