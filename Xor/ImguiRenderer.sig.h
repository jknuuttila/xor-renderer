#ifndef IMGUIRENDERER_SIG_H
#define IMGUIRENDERER_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(ImguiRenderer)


XOR_CBUFFER(Constants, 0)
{
    float2 reciprocalResolution;
};

XOR_TEXTURE_SRV(Texture2D<float>, tex, 0)

XOR_SAMPLER_POINT(pointSampler)


XOR_END_SIGNATURE

#define IMGUIRENDERER_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(1, 1)

#endif
