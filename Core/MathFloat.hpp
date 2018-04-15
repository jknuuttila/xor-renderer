#pragma once

#include "MathVectors.hpp"

#include <cmath>
#include <random>
#include <numeric>

namespace Xor
{
    constexpr float MaxFloat = std::numeric_limits<float>::max();
    static const float AlmostOne = std::nextafter(1.0f, 0.0f);

    inline float frac(float f)
    {
        float integral;
        return modf(f, &integral);
    }
    inline float2 frac(float2 f) { return float2(frac(f.x), frac(f.y)); }
    inline float3 frac(float3 f) { return float3(frac(f.x), frac(f.y), frac(f.z)); }
    inline float4 frac(float4 f) { return float4(frac(f.x), frac(f.y), frac(f.z), frac(f.w)); }

    template <uint N>
    inline Vector<float, N> saturate(Vector<float, N> a)
    {
        return clamp(a, 0, 1);
    }

    template <uint N>
    inline Vector<float, N> round(Vector<float, N> a)
    {
        for (uint i = 0; i < N; ++i)
            a[i] = std::round(a[i]);
        return a;
    }

    inline float2 normalizationMultiplyAdd(float a, float b)
    {
        if (b < a) std::swap(a, b);

        // (b - a) * s + a = x
        // (b - a) * s = x - a
        // s = (x - a) / (b - a)
        // s = x / (b - a) - a / (b - a)

        float s = 1.f / (b - a);
        float c = -a / (b - a);

        return { s, c };
    }

    template <typename T, typename U>
    inline T lerp(T a, T b, U alpha)
    {
        return a + (b - a) * alpha;
    }

    inline float remap(float a, float b, float c, float d, float x)
    {
        float alpha = (x - a) / (b - a);
        return lerp(c, d, alpha);
    }
    inline float2 remap(float2 a, float2 b, float2 c, float2 d, float2 x)
    {
        return float2(
            remap(a.x, b.x, c.x, d.x, x.x),
            remap(a.y, b.y, c.y, d.y, x.y));
    }
    inline float3 remap(float3 a, float3 b, float3 c, float3 d, float3 x)
    {
        return float3(
            remap(a.x, b.x, c.x, d.x, x.x),
            remap(a.y, b.y, c.y, d.y, x.y),
            remap(a.z, b.z, c.z, d.z, x.z));
    }
    inline float4 remap(float4 a, float4 b, float4 c, float4 d, float4 x)
    {
        return float4(
            remap(a.x, b.x, c.x, d.x, x.x),
            remap(a.y, b.y, c.y, d.y, x.y),
            remap(a.z, b.z, c.z, d.z, x.z),
            remap(a.w, b.w, c.w, d.w, x.w));
    }

    inline float clamp(float x, float minimum, float maximum)
    {
        return std::max(minimum, std::min(maximum, x));
    }

    // Quadratic equation of the form ax^2 + bx + c == 0
    struct Quadratic
    {
        float a = 0;
        float b = 0;
        float c = 0;

        Quadratic() = default;
        Quadratic(float a, float b, float c) : a(a), b(b), c(c) {}

        float discriminant() const
        {
            return b*b - 4*a*c;
        }

        struct Roots
        {
            float2 x;
            int numRoots = 0;

            Roots() = default;
            Roots(float x) : x(x), numRoots(1) {}
            Roots(float x0, float x1) : x(x0, x1), numRoots(2) {}

            explicit operator bool() const { return numRoots > 0; }
        };

        Roots solve() const
        {
            float D = discriminant();

            if (D < 0)
            {
                return Roots();
            }
            else if (D == 0)
            {
                return Roots(-b / (2 * a));
            }
            else
            {
                float d    = sqrt(D);
                float a2   = 2 * a;
                return Roots((-b + d) / a2, (-b - d) / a2);
            }
        }
    };
}
