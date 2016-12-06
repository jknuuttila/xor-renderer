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
            edgeFunction01(a, b, c, p) >= 0 &&
            edgeFunction12(a, b, c, p) >= 0 &&
            edgeFunction20(a, b, c, p) >= 0;
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

    template <typename T>
    inline T interpolateBarycentric(T a, T b, T c, float3 bary)
    {
        return a * bary.x
            +  b * bary.y
            +  c * bary.z;
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

    struct Circle
    {
        float2 center;
        float radiusSqr = 0;

        Circle() = default;
        Circle(float2 center, float radius)
            : center(center)
            , radiusSqr(radius * radius)
        {}

        float radius() const { return sqrt(radiusSqr); }

        bool contains(float2 p) const
        {
            return (center - p).lengthSqr() <= radiusSqr;
        }
    };

    // From https://en.wikipedia.org/wiki/Circumscribed_circle
    inline Circle circumcircle(float2 A, float2 B, float2 C)
    {
        float A2 = A.lengthSqr();
        float B2 = B.lengthSqr();
        float C2 = C.lengthSqr();

        float2 S;
        S.x = 0.5f * float3x3(A2, A.y, 1.f,
                              B2, B.y, 1.f,
                              C2, C.y, 1.f).determinant();

        S.y = 0.5f * float3x3(A.x, A2, 1.f,
                              B.x, B2, 1.f,
                              C.x, C2, 1.f).determinant();

        float a = float3x3(A.x, A.y, 1.f,
                           B.x, B.y, 1.f,
                           C.x, C.y, 1.f).determinant();
        float b = float3x3(A.x, A.y, A2,
                           B.x, B.y, B2,
                           C.x, C.y, C2).determinant();

        Circle cc;
        cc.center    = S / a;
        cc.radiusSqr = b/a + S.lengthSqr() / (a*a);
        return cc;
    }

    // Return a number that is zero if all the points are on the same circle.
    // If points p1, p2 and p3 define a circle but p4 is not on the circle,
    // the sign of the determinant is different whether p4 is inside or outside
    // the circle. However, the sign which is inside the circle is undefined,
    // and must be checked separately by e.g. testing with a point that is
    // definitely inside or outside the circle.
    inline float pointsOnCircle(float2 p1, float2 p2, float2 p3, float2 p4)
    {
        float N1 = p1.lengthSqr();
        float N2 = p2.lengthSqr();
        float N3 = p3.lengthSqr();
        float N4 = p4.lengthSqr();

        return float4x4(N1, p1.x, p1.y, 1.f,
                        N2, p2.x, p2.y, 1.f,
                        N3, p3.x, p3.y, 1.f,
                        N4, p4.x, p4.y, 1.f).determinant();
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

