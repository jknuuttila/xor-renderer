#pragma once

#include "MathVectors.hpp"
#include "MathFloat.hpp"

#include <random>

namespace xor
{
	inline float orient2D(float2 a, float2 b, float2 c)
	{
		return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
	}

	inline bool isTriangleCCW(float2 a, float2 b, float2 c)
	{
		return orient2D(a, b, c) > 0;
	}

    inline float edgeFunction(float2 v0, float2 v1, float2 p)
    {
        return (v0.y - v1.y) * p.x + (v1.x - v0.x) * p.y + (v0.x * v1.y - v0.y * v1.x);
    }
    inline float edgeFunction01(float2 a, float2 b, float2 c, float2 p) { return edgeFunction(a, b, p); }
    inline float edgeFunction12(float2 a, float2 b, float2 c, float2 p) { return edgeFunction(b, c, p); }
    inline float edgeFunction20(float2 a, float2 b, float2 c, float2 p) { return edgeFunction(c, a, p); }

    inline bool isPointInsideTriangle(float2 a, float2 b, float2 c, float2 p)
    {
        return
            edgeFunction01(a, b, c, p) > 0 &&
            edgeFunction12(a, b, c, p) > 0 &&
            edgeFunction20(a, b, c, p) > 0;
    }

    inline float triangleDoubleSignedArea(float2 a, float2 b, float2 c)
    {
        return orient2D(a, b, c);
    }
    inline float triangleSignedArea(float2 a, float2 b, float2 c)
    {
        return triangleDoubleSignedArea(a, b, c) / 2;
    }
    inline float3 barycentric(float2 a, float2 b, float2 c, float2 p, float doubleSignedArea)
    {
        return float3(edgeFunction01(a, b, c, p),
                      edgeFunction12(a, b, c, p),
                      edgeFunction20(a, b, c, p)) / doubleSignedArea;
    }
    inline float3 barycentric(float2 a, float2 b, float2 c, float2 p)
    {
        return barycentric(a, b, c, triangleDoubleSignedArea(a, b, c));
    }

    // Test if the quadrilateral ABCD is convex. Vertices B and C should
    // be adjacent to both A and D.
    inline bool isQuadConvex(float2 a, float2 b, float2 c, float2 d)
    {
        // It is convex iff the vertex D lies on different sides
        // of the directed edges AB and AC
        float ABD = orient2D(a, b, d);
        float ACD = orient2D(a, c, d);

        // It is on different sides if the sign of the product is negative
        return (ABD * ACD < 0);
    }

    // From http://mathworld.wolfram.com/Circumcircle.html
    inline float2 circumcircleCenter(float2 p1, float2 p2, float2 p3)
    {
        float3x3 A(
            p1.x, p1.y, 1.f,
            p2.x, p2.y, 1.f,
            p3.x, p3.y, 1.f);
        float a = A.determinant();

        float2 p12 = p1 * p1;
        float2 p22 = p2 * p2;
        float2 p32 = p3 * p3;

        float3x3 Bx(
            p12.x + p12.y, p1.y, 1.f,
            p22.x + p22.y, p2.y, 1.f,
            p32.x + p32.y, p3.y, 1.f);
        float bx = -Bx.determinant();

        float3x3 By(
            p12.x + p12.y, p1.x, 1.f,
            p22.x + p22.y, p2.x, 1.f,
            p32.x + p32.y, p3.x, 1.f);
        float by = By.determinant();

        float a2 = 2 * a;

        float x0 = -bx / a2;
        float y0 = -by / a2;

        return { x0, y0 };
    }

	template <typename RandomGen>
	float3 uniformBarycentric(RandomGen &gen)
	{
		std::uniform_real_distribution<float> U;

		float r1  = U(gen);
		float r2  = U(gen);
		float sr1 = sqrt(r1);

		return float3(1 - sr1, sr1 * (1 - r2), r2 * sr1);
	}
}

