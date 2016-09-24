#pragma once

#include "MathVectors.hpp"
#include <cmath>

namespace xor
{
    inline float frac(float f)
    {
        float integral;
        return modf(f, &integral);
    }

    // https://en.wikipedia.org/wiki/HSL_and_HSV#From_HSV
    inline float3 hsvToRGB(float3 hsv)
    {
        float H  = hsv.x * 360.f;
        float C  = hsv.y * hsv.z;
        float V  = hsv.z;
        float H_ = H / 60.f;
        float X  = C * (1.f - abs(fmod(H_, 2.f) - 1.f));

        uint face = static_cast<uint>(floor(H_));

        float3 rgb1;

        switch (face)
        {
        case 0: rgb1 = float3(C, X, 0); break;
        case 1: rgb1 = float3(X, C, 0); break;
        case 2: rgb1 = float3(0, C, X); break;
        case 3: rgb1 = float3(0, X, C); break;
        case 4: rgb1 = float3(X, 0, C); break;
        case 5: rgb1 = float3(C, 0, X); break;
        default:
            return float3(0);
        }

        float m = V - C;
        return rgb1 + float3(m);
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

}
