#pragma once

#include "Core/MathVectors.hpp"
#include "Core/MathFloat.hpp"

namespace xor
{
    struct ColorUnorm
    {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 1;

        ColorUnorm() = default;

        ColorUnorm(uint32_t rgba)
        {
            r = uint8_t(rgba & 0xff);
            g = uint8_t((rgba >> 8) & 0xff);
            b = uint8_t((rgba >> 16) & 0xff);
            a = uint8_t((rgba >> 24) & 0xff);
        }

        ColorUnorm(float4 color)
        {
            color = clamp(round(color * 255.f), float4(0.f), float4(255.f));
            r     = uint8_t(color.x);
            g     = uint8_t(color.y);
            b     = uint8_t(color.z);
            a     = uint8_t(color.w);
        }

        float4 toFloat4() const
        {
            float4 color { float(r), float(g), float(b), float(a) };
            return color / 255.f;
        }
    };

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
}

