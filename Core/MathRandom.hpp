#pragma once
#pragma once

#include "MathVectors.hpp"
#include "MathFloat.hpp"
#include "MathRandomXoroshiro128p.hpp"

#include <random>

namespace Xor
{
    inline float3 uniformBarycentric(float2 u)
    {
        float r1  = u.x;
        float r2  = u.y;
        float sr1 = sqrt(r1);

        return float3(1 - sr1, sr1 * (1 - r2), r2 * sr1);
    }

    template <typename RandomGen>
    float3 uniformBarycentricGen(RandomGen &gen)
    {
        std::uniform_real_distribution<float> U;
        return uniformBarycentric(float2(U(gen), U(gen)));
    }

    inline float2 uniformDisk(float2 u)
    {
        float r  = u.x;
        float th = u.y * (2 * Pi);

        float sr = sqrt(r);

        return float2(sr * cos(th), sr * sin(th));
    }

    template <typename RandomGen>
    float2 uniformDiskGen(RandomGen &gen)
    {
        std::uniform_real_distribution<float> U;
        return uniformDisk(float2(U(gen), U(gen)));
    }

    // Taken from http://www.rorydriscoll.com/2009/01/07/better-sampling/
    inline float3 uniformHemisphere(float2 u)
    {
        float u1 = u.x;
        float u2 = u.y;

        const float r = sqrt(1.0f - u1 * u1);
        const float phi = (2 * Pi) * u2;
     
        return float3(cos(phi) * r, sin(phi) * r, u1);
    }

    inline float3 uniformSphere(float2 u)
    {
        const float theta  = u.x * (2 * Pi);
        const float cosPhi = 2*u.y - 1;
        const float k      = sqrt(1 - cosPhi*cosPhi);

        return float3(k * cos(theta),
                      k * sin(theta),
                      cosPhi);
    }

    inline float3 cosineWeightedHemisphere(float2 u)
    {
        float2 disk = uniformDisk(u);
        return float3(disk.x, disk.y, sqrt(std::max(0.f, 1.f - u.x)));
    }

    template <typename RandomGen>
    float3 uniformHemisphereGen(RandomGen &gen)
    {
        std::uniform_real_distribution<float> U;
        return uniformHemisphere(float2(U(gen), U(gen)));
    }

    template <typename RandomGen>
    float3 uniformSphereGen(RandomGen &gen)
    {
        std::uniform_real_distribution<float> U;
        return uniformSphere(float2(U(gen), U(gen)));
    }

    template <typename RandomGen>
    float3 cosineWeightedHemisphereGen(RandomGen &gen)
    {
        std::uniform_real_distribution<float> U;
        return cosineWeightedHemisphere(float2(U(gen), U(gen)));
    }

}
