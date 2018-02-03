#ifndef XOR_SHADERMATH_H_HLSL
#define XOR_SHADERMATH_H_HLSL

static const float Pi = 3.141592654;

// Sometimes referred to as Chi+
float nonNegative(float x)
{
    return max(0, x);
}

float remap(float a, float b, float c, float d, float x)
{
	float alpha = (x - a) / (b - a);
	return lerp(c, d, alpha);
}
float2 remap(float2 a, float2 b, float2 c, float2 d, float2 x)
{
	return float2(
		remap(a.x, b.x, c.x, d.x, x.x),
		remap(a.y, b.y, c.y, d.y, x.y));
}
float3 remap(float3 a, float3 b, float3 c, float3 d, float3 x)
{
	return float3(
		remap(a.x, b.x, c.x, d.x, x.x),
		remap(a.y, b.y, c.y, d.y, x.y),
		remap(a.z, b.z, c.z, d.z, x.z));
}
float4 remap(float4 a, float4 b, float4 c, float4 d, float4 x)
{
	return float4(
		remap(a.x, b.x, c.x, d.x, x.x),
		remap(a.y, b.y, c.y, d.y, x.y),
		remap(a.z, b.z, c.z, d.z, x.z),
		remap(a.w, b.w, c.w, d.w, x.w));
}

float2 ndcToUV(float2 ndc)
{
    float2 zeroToOne = ndc / 2 + 0.5;
    float2 uv = float2(
        zeroToOne.x,
        1 - zeroToOne.y);
    return uv;
}

float2 uvToNDC(float2 uv)
{
    return lerp(float2(-1, 1), float2(1, -1), uv);
}

float3 signedColor(float value, float max)
{
	float V = abs(value) / max;
	if (V >= 0)
		return float3(V, 0, 0);
	else
		return float3(0, 0, V);
}

// Optimized GGX specular implementation from
// http://www.filmicworlds.com/2014/04/21/optimizing-ggx-shaders-with-dotlh/

/*
optimized-ggx.hlsl
This file describes several optimizations you can make when creating a GGX shader.

AUTHOR: John Hable

LICENSE:

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

float G1V(float dotNV, float k)
{
	return 1.0f/(dotNV*(1.0f-k)+k);
}

float GGX_REF(float3 N, float3 V, float3 L, float roughness, float F0)
{
	float alpha = roughness*roughness;

	float3 H = normalize(V+L);

	float dotNL = saturate(dot(N,L));
	float dotNV = saturate(dot(N,V));
	float dotNH = saturate(dot(N,H));
	float dotLH = saturate(dot(L,H));

	float F, D, vis;

	// D
	float alphaSqr = alpha*alpha;
	float pi = 3.14159f;
	float denom = dotNH * dotNH *(alphaSqr-1.0) + 1.0f;
	D = alphaSqr/(pi * denom * denom);

	// F
	float dotLH5 = pow(1.0f-dotLH,5);
	F = F0 + (1.0-F0)*(dotLH5);

	// V
	float k = alpha/2.0f;
	vis = G1V(dotNL,k)*G1V(dotNV,k);

	float specular = dotNL * D * F * vis;
	return specular;
}

float GGX_OPT1(float3 N, float3 V, float3 L, float roughness, float F0)
{
	float alpha = roughness*roughness;

	float3 H = normalize(V+L);

	float dotNL = saturate(dot(N,L));
	float dotLH = saturate(dot(L,H));
	float dotNH = saturate(dot(N,H));

	float F, D, vis;

	// D
	float alphaSqr = alpha*alpha;
	float pi = 3.14159f;
	float denom = dotNH * dotNH *(alphaSqr-1.0) + 1.0f;
	D = alphaSqr/(pi * denom * denom);

	// F
	float dotLH5 = pow(1.0f-dotLH,5);
	F = F0 + (1.0-F0)*(dotLH5);

	// V
	float k = alpha/2.0f;
	vis = G1V(dotLH,k)*G1V(dotLH,k);

	float specular = dotNL * D * F * vis;
	return specular;
}


float GGX_OPT2(float3 N, float3 V, float3 L, float roughness, float F0)
{
	float alpha = roughness*roughness;

	float3 H = normalize(V+L);

	float dotNL = saturate(dot(N,L));

	float dotLH = saturate(dot(L,H));
	float dotNH = saturate(dot(N,H));

	float F, D, vis;

	// D
	float alphaSqr = alpha*alpha;
	float pi = 3.14159f;
	float denom = dotNH * dotNH *(alphaSqr-1.0) + 1.0f;
	D = alphaSqr/(pi * denom * denom);

	// F
	float dotLH5 = pow(1.0f-dotLH,5);
	F = F0 + (1.0-F0)*(dotLH5);

	// V
	float k = alpha/2.0f;
	float k2 = k*k;
	float invK2 = 1.0f-k2;
	vis = rcp(dotLH*dotLH*invK2 + k2);

	float specular = dotNL * D * F * vis;
	return specular;
}

float2 GGX_FV(float dotLH, float roughness)
{
	float alpha = roughness*roughness;

	// F
	float F_a, F_b;
	float dotLH5 = pow(1.0f-dotLH,5);
	F_a = 1.0f;
	F_b = dotLH5;

	// V
	float vis;
	float k = alpha/2.0f;
	float k2 = k*k;
	float invK2 = 1.0f-k2;
	vis = rcp(dotLH*dotLH*invK2 + k2);

	return float2(F_a*vis,F_b*vis);
}

float GGX_D(float dotNH, float roughness)
{
	float alpha = roughness*roughness;
	float alphaSqr = alpha*alpha;
	float pi = 3.14159f;
	float denom = dotNH * dotNH *(alphaSqr-1.0) + 1.0f;

	float D = alphaSqr/(pi * denom * denom);
	return D;
}

float GGX_OPT3(float3 N, float3 V, float3 L, float roughness, float F0)
{
	float3 H = normalize(V+L);

	float dotNL = saturate(dot(N,L));
	float dotLH = saturate(dot(L,H));
	float dotNH = saturate(dot(N,H));

	float D = GGX_D(dotNH,roughness);
	float2 FV_helper = GGX_FV(dotLH,roughness);
	float FV = F0*FV_helper.x + (1.0f-F0)*FV_helper.y;
	float specular = dotNL * D * FV;

	return specular;
}

float GGX(float3 N, float3 V, float3 L, float roughness, float F0)
{
    return GGX_OPT3(N, V, L, roughness, F0);
}

static const uint FNV32Prime = 0x01000193;
static const uint FNV32Basis = 0x811C9DC5;

uint fnv1AInit()
{
    return FNV32Basis;
}

uint fnv1AConsume8(uint hash, uint octet)
{
    hash ^= octet;
    hash *= FNV32Prime;
    return hash;
}

uint fnv1AConsume32(uint hash, uint data32)
{
    hash = fnv1AConsume8(hash,         data32 & 0xff);
    hash = fnv1AConsume8(hash, (data32 >>  8) & 0xff);
    hash = fnv1AConsume8(hash, (data32 >> 16) & 0xff);
    hash = fnv1AConsume8(hash, (data32 >> 24) & 0xff);
    return hash;
}

uint fnv1AHash32(uint data32)
{
    uint hash = fnv1AInit();
    return fnv1AConsume32(hash, data32);
}

// Algorithm from 
// https://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both
float3 HSVToRGB(float3 hsv)
{
    hsv = saturate(hsv);

    float H = hsv.x;
    float S = hsv.y;
    float V = hsv.z;

    H *= 6;
    float f = frac(H);

    float P = V * (1 - S);
    float Q = V * (1 - S * f);
    float T = V * (1 - S * (1 - f));

    if (H <= 1)
        return float3(V, T, P);
    else if (H <= 2)
        return float3(Q, V, P);
    else if (H <= 3)
        return float3(P, V, T);
    else if (H <= 4)
        return float3(P, Q, V);
    else if (H <= 5)
        return float3(T, P, V);
    else if (H <= 6)
        return float3(V, P, Q);
    else
        return float3(0, 0, 0);
}

float3 randomColorFromHash(uint hash)
{
    float3 hsv;
    hsv.x = float(hash & 0xffff) / float(0xffff);
    hsv.y = 1;
    hsv.z = lerp(0.5, 1, float((hash >> 16) & 0xfff) / float(0xffff));
    return HSVToRGB(hsv);
}

float3 randomColorFromUint(uint x)
{
    return randomColorFromHash(fnv1AHash32(x));
}

#if 0
float4 bicubicBSpline(Texture2D<float4> tex, SamplerState bilinear, float2 uv)
{
    float2 iTc = uv;

    float2 texSize;
    tex.GetDimensions( texSize.x, texSize.y );
    float2 invTexSize = 1.0 / texSize;
 
    iTc *= texSize;

    //round tc *down* to the nearest *texel center*
 
    float2 tc = floor( iTc - 0.5 ) + 0.5;

    //compute the fractional offset from that texel center
    //to the actual coordinate we want to filter at
 
    float2 f = iTc - tc;
 
    //we'll need the second and third powers
    //of f to compute our filter weights
 
    float2 f2 = f * f;
    float2 f3 = f2 * f;
 
    //compute the filter weights
 
    float2 a  = f;
    float2 w0 = (1.0 / 6.0)*(a*(a*(-a + 3.0) - 3.0) + 1.0);
    float2 w1 = (1.0 / 6.0)*(a*a*(3.0*a - 6.0) + 4.0);
    float2 w2 = (1.0 / 6.0)*(a*(a*(-3.0*a + 3.0) + 3.0) + 1.0);
    float2 w3 = (1.0 / 6.0)*(a*a*a);

    //get our texture coordinates
 
    float2 tc0 = tc - 1;
    float2 tc1 = tc;
    float2 tc2 = tc + 1;
    float2 tc3 = tc + 2;
 
    /*
        If we're only using a portion of the texture,
        this is where we need to clamp tc2 and tc3 to
        make sure we don't sample off into the unused
        part of the texture (tc0 and tc1 only need to
        be clamped if our subrectangle doesn't start
        at the origin).
    */
 
    //convert them to normalized coordinates
 
    tc0 *= invTexSize;
    tc1 *= invTexSize;
    tc2 *= invTexSize;
    tc3 *= invTexSize;

    //get our texture coordinates
 
    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
 
    float2 f0 = w1 / (w0 + w1);
    float2 f1 = w3 / (w2 + w3);
 
    float2 t0 = tc - 1 + f0;
    float2 t1 = tc + 1 + f1;
 
    //and sample and blend
 
#if 1
    return
        (tex.Sample(bilinear, float2( t0.x, t0.y ) ) * s0.x
      +  tex.Sample(bilinear, float2( t1.x, t0.y ) ) * s1.x) * s0.y
      + (tex.Sample(bilinear, float2( t0.x, t1.y ) ) * s0.x
      +  tex.Sample(bilinear, float2( t1.x, t1.y ) ) * s1.x) * s1.y;
#else
    return
        tex.Sample(bilinear, float2( t0.x, t0.y ) ) * s0.x * s0.y
      + tex.Sample(bilinear, float2( t1.x, t0.y ) ) * s1.x * s0.y
      + tex.Sample(bilinear, float2( t0.x, t1.y ) ) * s0.x * s1.y
      + tex.Sample(bilinear, float2( t1.x, t1.y ) ) * s1.x * s1.y;
#endif
#if 0
    //--------------------------------------------------------------------------------------
    // Calculate the center of the texel to avoid any filtering

    float2 textureDimensions    = 128;//GetTextureDimensions( tex );
    float2 invTextureDimensions = 1.f / textureDimensions;

    uv *= textureDimensions;

    float2 texelCenter   = floor( uv - 0.5f ) + 0.5f;
    float2 fracOffset    = uv - texelCenter;
    float2 fracOffset_x2 = fracOffset * fracOffset;
    float2 fracOffset_x3 = fracOffset * fracOffset_x2;

    //--------------------------------------------------------------------------------------
    // Calculate the filter weights (B-Spline Weighting Function)

    float2 weight0 = fracOffset_x2 - 0.5f * ( fracOffset_x3 + fracOffset );
    float2 weight1 = 1.5f * fracOffset_x3 - 2.5f * fracOffset_x2 + 1.f;
    float2 weight3 = 0.5f * ( fracOffset_x3 - fracOffset_x2 );
    float2 weight2 = 1.f - weight0 - weight1 - weight3;

    //--------------------------------------------------------------------------------------
    // Calculate the texture coordinates

    float2 scalingFactor0 = weight0 + weight1;
    float2 scalingFactor1 = weight2 + weight3;

    float2 f0 = weight1 / ( weight0 + weight1 );
    float2 f1 = weight3 / ( weight2 + weight3 );

    float2 texCoord0 = texelCenter - 1.f + f0;
    float2 texCoord1 = texelCenter + 1.f + f1;

    texCoord0 *= invTextureDimensions;
    texCoord1 *= invTextureDimensions;

    //--------------------------------------------------------------------------------------
    // Sample the texture

    return tex.Sample( bilinear, float2( texCoord0.x, texCoord0.y ) ) * scalingFactor0.x * scalingFactor0.y +
           tex.Sample( bilinear, float2( texCoord1.x, texCoord0.y ) ) * scalingFactor1.x * scalingFactor0.y +
           tex.Sample( bilinear, float2( texCoord0.x, texCoord1.y ) ) * scalingFactor0.x * scalingFactor1.y +
           tex.Sample( bilinear, float2( texCoord1.x, texCoord1.y ) ) * scalingFactor1.x * scalingFactor1.y;
#endif
}
#else

// FIXME: This code is from Shadertoy and not MIT licensed, need to rewrite

// w0, w1, w2, and w3 are the four cubic B-spline basis functions
float w0(float a)
{
    return (1.0/6.0)*(a*(a*(-a + 3.0) - 3.0) + 1.0);
}

float w1(float a)
{
    return (1.0/6.0)*(a*a*(3.0*a - 6.0) + 4.0);
}

float w2(float a)
{
    return (1.0/6.0)*(a*(a*(-3.0*a + 3.0) + 3.0) + 1.0);
}

float w3(float a)
{
    return (1.0/6.0)*(a*a*a);
}

// g0 and g1 are the two amplitude functions
float g0(float a)
{
    return w0(a) + w1(a);
}

float g1(float a)
{
    return w2(a) + w3(a);
}

// h0 and h1 are the two offset functions
float h0(float a)
{
    return -1.0 + w1(a) / (w0(a) + w1(a));
}

float h1(float a)
{
    return 1.0 + w3(a) / (w2(a) + w3(a));
}

struct BicubicBSplineWeights
{
    float g0x;
    float g1x;
    float g0y;
    float g1y;
	float2 p0;
	float2 p1;
	float2 p2;
	float2 p3;
};

BicubicBSplineWeights bicubicBSplineWeights(float2 uv, float2 textureSize)
{
    float2 invTextureSize = 1 / textureSize;

	uv = uv*textureSize + 0.5;
	float2 iuv = floor( uv );
	float2 fuv = frac( uv );

    float g0x = g0(fuv.x);
    float g1x = g1(fuv.x);
    float h0x = h0(fuv.x);
    float h1x = h1(fuv.x);
    float h0y = h0(fuv.y);
    float h1y = h1(fuv.y);

	float2 p0 = (float2(iuv.x + h0x, iuv.y + h0y) - 0.5) * invTextureSize;
	float2 p1 = (float2(iuv.x + h1x, iuv.y + h0y) - 0.5) * invTextureSize;
	float2 p2 = (float2(iuv.x + h0x, iuv.y + h1y) - 0.5) * invTextureSize;
	float2 p3 = (float2(iuv.x + h1x, iuv.y + h1y) - 0.5) * invTextureSize;

    BicubicBSplineWeights weights;
    weights.g0x = g0x;
    weights.g1x = g1x;
    weights.g0y = g0(fuv.y);
    weights.g1y = g1(fuv.y);
    weights.p0  = p0;
    weights.p1  = p1;
    weights.p2  = p2;
    weights.p3  = p3;

    return weights;
}
	
#define BICUBIC_SAMPLE(texture_, bilinear_, weights_) \
    return weights_.g0y * (weights_.g0x * texture_.Sample(bilinear_, weights_.p0)  + \
                           weights_.g1x * texture_.Sample(bilinear_, weights_.p1)) + \
           weights_.g1y * (weights_.g0x * texture_.Sample(bilinear_, weights_.p2)  + \
                           weights_.g1x * texture_.Sample(bilinear_, weights_.p3));
#define BICUBIC_SAMPLE_CMP(texture_, cmpBilinear_, weights_, cmp_) \
    return weights_.g0y * (weights_.g0x * texture_.SampleCmp(cmpBilinear_, weights_.p0, cmp_)  + \
                           weights_.g1x * texture_.SampleCmp(cmpBilinear_, weights_.p1, cmp_)) + \
           weights_.g1y * (weights_.g0x * texture_.SampleCmp(cmpBilinear_, weights_.p2, cmp_)  + \
                           weights_.g1x * texture_.SampleCmp(cmpBilinear_, weights_.p3, cmp_));

float sampleBicubicBSpline(Texture2D<float> tex, SamplerState bilinear, float2 uv, float2 textureSize)
{
    BicubicBSplineWeights weights = bicubicBSplineWeights(uv, textureSize);
    BICUBIC_SAMPLE(tex, bilinear, weights);
}
float2 sampleBicubicBSpline(Texture2D<float2> tex, SamplerState bilinear, float2 uv, float2 textureSize)
{
    BicubicBSplineWeights weights = bicubicBSplineWeights(uv, textureSize);
    BICUBIC_SAMPLE(tex, bilinear, weights);
}
float3 sampleBicubicBSpline(Texture2D<float3> tex, SamplerState bilinear, float2 uv, float2 textureSize)
{
    BicubicBSplineWeights weights = bicubicBSplineWeights(uv, textureSize);
    BICUBIC_SAMPLE(tex, bilinear, weights);
}
float4 sampleBicubicBSpline(Texture2D<float4> tex, SamplerState bilinear, float2 uv, float2 textureSize)
{
    BicubicBSplineWeights weights = bicubicBSplineWeights(uv, textureSize);
    BICUBIC_SAMPLE(tex, bilinear, weights);
}
float sampleCmpBicubicBSpline(Texture2D<float> tex, SamplerComparisonState cmpBilinear, float2 uv, float cmp, float2 textureSize)
{
    BicubicBSplineWeights weights = bicubicBSplineWeights(uv, textureSize);
    BICUBIC_SAMPLE_CMP(tex, cmpBilinear, weights, cmp);
}

#undef BICUBIC_SAMPLE
#undef BICUBIC_SAMPLE_CMP

#endif

#endif
