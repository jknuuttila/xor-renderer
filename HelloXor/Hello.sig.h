#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(Hello)


XOR_CBUFFER(OffsetConstants, 0)
{
    float2 offset;
};

XOR_CBUFFER(SizeConstants, 1)
{
    float2 size;
};

XOR_SRV(Texture2D<float4>, tex, 0)

XOR_SAMPLER_BILINEAR(bilinear)


XOR_END_SIGNATURE

#define HELLO_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(2, 1)
