#pragma once

#include "MathVectors.hpp"
#include "MathFloat.hpp"

#include <random>

namespace xor
{
    template <typename T>
	inline T orient2D(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c)
	{
		return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
	}

    template <typename T>
	inline bool isTriangleCCW(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c)
	{
		return orient2D(a, b, c) > 0;
	}

    template <typename T>
    inline T edgeFunction(Vector<T, 2> v0, Vector<T, 2> v1, Vector<T, 2> p)
    {
        T A = (v0.y - v1.y);
        T B = (v1.x - v0.x);
        T C = (v0.x * v1.y);
        T D = (v0.y * v1.x);
        T E = A * p.x;
        T F = B * p.y;
        T G = C - D;
        T H = E + F + G;
        return (v0.y - v1.y) * p.x + (v1.x - v0.x) * p.y + (v0.x * v1.y - v0.y * v1.x);
    }
    template <typename T>
    inline T edgeFunction01(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> p) { return edgeFunction(a, b, p); }
    template <typename T>
    inline T edgeFunction12(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> p) { return edgeFunction(b, c, p); }
    template <typename T>
    inline T edgeFunction20(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> p) { return edgeFunction(c, a, p); }

    template <typename T>
    inline bool isPointInsideTriangle(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> p)
    {
        return
            edgeFunction01(a, b, c, p) >= 0 &&
            edgeFunction12(a, b, c, p) >= 0 &&
            edgeFunction20(a, b, c, p) >= 0;
    }

    template <typename T>
    inline bool isPointInsideTriangleUnknownWinding(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> p)
    {
        if (isTriangleCCW(a, b, c))
            return isPointInsideTriangle(a, b, c, p);
        else
            return isPointInsideTriangle(a, c, b, p);
    }

    template <typename T>
    inline T triangleDoubleSignedArea(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c)
    {
        return orient2D(a, b, c);
    }
    template <typename T>
    inline float triangleSignedArea(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c)
    {
        return triangleDoubleSignedArea(a, b, c) / 2;
    }
    template <typename T>
    inline float3 barycentric(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> p, T doubleSignedArea)
    {
        return float3(edgeFunction01(a, b, c, p),
                      edgeFunction12(a, b, c, p),
                      edgeFunction20(a, b, c, p)) / float(doubleSignedArea);
    }
    template <typename T>
    inline float3 barycentric(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> p)
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
    template <typename T>
    inline bool isQuadConvex(Vector<T, 2> a, Vector<T, 2> b, Vector<T, 2> c, Vector<T, 2> d)
    {
        // It is convex iff the vertex D lies on different sides
        // of the directed edges AB and AC
        T ABD = orient2D(a, b, d);
        T ACD = orient2D(a, c, d);

        // It is on different sides if the sign of the product is negative
        return (ABD * ACD < 0);
    }

    template <typename T>
    struct Circle
    {
        Vector<T, 2> center;
        T radiusSqr = 0;

        Circle() = default;
        Circle(Vector<T, 2> center, T radius)
            : center(center)
            , radiusSqr(radius * radius)
        {}

        float radius() const { return sqrt(float(radiusSqr)); }

        bool contains(Vector<T, 2> p) const
        {
            return (center - p).lengthSqr() <= radiusSqr;
        }

        T power(Vector<T, 2> p) const
        {
            return (center - p).lengthSqr() - radiusSqr;
        }
    };

    // From https://en.wikipedia.org/wiki/Circumscribed_circle
    inline Circle<float> circumcircle(float2 A, float2 B, float2 C)
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

        Circle<float> cc;
        cc.center    = S / a;
        cc.radiusSqr = b/a + S.lengthSqr() / (a*a);
        return cc;
    }

    // Return a number that is zero if all the points are on the same circle.
    // If points p1, p2 and p3 define a circle but p4 is not on the circle,
    // the sign of the determinant is different whether p4 is inside or outside
    // the circle. However, the sign depends on the triangle winding.
    // TODO: Define which way the winding goes.
    template <typename T>
    inline T inCircle(Vector<T, 2> p1, Vector<T, 2> p2, Vector<T, 2> p3, Vector<T, 2> p4)
    {
        T N1 = p1.lengthSqr();
        T N2 = p2.lengthSqr();
        T N3 = p3.lengthSqr();
        T N4 = p4.lengthSqr();

        return Mat<T, 4, 4>(N1, p1.x, p1.y, 1.f,
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

