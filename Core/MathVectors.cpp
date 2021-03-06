#include "MathVectors.hpp"

namespace Xor
{
    namespace math
    {
        AxisAngleRotation::AxisAngleRotation(float3 axis, Angle angle)
            : axis(axis)
            , cosAngle(cos(angle.radians))
            , sinAngle(sin(angle.radians))
        {}

        AxisAngleRotation AxisAngleRotation::fromTo(float3 a, float3 b)
        {
            float cosAngle = dot(a, b);

            float3 k       = cross(a, b);
            float kLen     = k.length();

            float sinAngle = kLen;

            constexpr float CollinearThreshold = .001f;

            if (std::abs(sinAngle) < CollinearThreshold)
            {
                if (cosAngle < 0)
                    return AxisAngleRotation();
                else
                    return AxisAngleRotation(float3(1, 0, 0), -1, 0);
            }
            else
            {
                return AxisAngleRotation(k * (1 / kLen), cosAngle, sinAngle);
            }
        }

        float3 AxisAngleRotation::rotate(float3 v) const
        {
            auto &k = axis;
            float s = sinAngle;
            float c = cosAngle;

            return v*c + cross(k, v)*s + k*dot(k, v)*(1-c);
        }

        Matrix Matrix::crossProductMatrix(float3 k)
        {
            return Matrix {
                {    0, -k.z,  k.y, 0 },
                {  k.z,    0, -k.x, 0 },
                { -k.y,  k.x,    0, 0 },
                {    0 ,   0,    0, 0 },
            };
        }

        Matrix Matrix::axisAngle(float3 axis, float cosAngle, float sinAngle)
        {
            float3 k = axis;
            float  s = sinAngle;
            float  c = cosAngle;

            Matrix K = Matrix::crossProductMatrix(k);
            Matrix R = Matrix::identity() + s*K + (1-c) * (K*K);
            return R;
        }

        Matrix Matrix::axisAngle(float3 axis, Angle angle)
        {
            return axisAngle(axis,
                             cos(angle.radians),
                             sin(angle.radians));
        }

        Matrix Matrix::rotateFromTo(float3 a, float3 b)
        {
            float cosAngle = dot(a, b);

            float3 k       = cross(a, b);
            float kLen     = k.length();

            float sinAngle = kLen;

            constexpr float CollinearThreshold = .001f;

            if (std::abs(sinAngle) < CollinearThreshold)
            {
                if (cosAngle < 0)
                {
                    return Matrix {
                        { -1,  0,  0,  0 },
                        {  0, -1,  0,  0 },
                        {  0,  0, -1,  0 },
                        {  0,  0,  0,  1 },
                    };
                }
                else
                {
                    return Matrix::identity();
                }
            }
            else
            {
                return Matrix::axisAngle(k * (1 / kLen), cosAngle, sinAngle);
            }
        }

        Matrix Matrix::lookInDirection(float3 dir, float3 up)
        {
            float3 back  = -dir;
            float3 right = cross(up, back);
            float3 up_   = cross(back, right);

            return Matrix {
                float4(right),
                float4(up_),
                float4(back),
                { 0, 0, 0, 1 }
            };
        }

        Matrix Matrix::lookTo(float3 pos, float3 dir, float3 up)
        {
            return lookInDirection(dir, up) * translation(-pos);
        }

        Matrix Matrix::lookAt(float3 pos, float3 target, float3 up)
        {
            static const float3 DefaultFront = { 0, 0, -1 };
            return lookTo(pos, normalize(target - pos, DefaultFront), up);
        }

        Matrix Matrix::projectionPerspective(float aspectRatioWByH, Angle verticalFov, float depth1Plane, float depth0Plane)
        {
            // Right handed coordinates, so flip Z, but respect if the depths were already
            // given in right handed view space
            depth1Plane = -std::abs(depth1Plane);
            depth0Plane = -std::abs(depth0Plane);

            float2 imagePlaneSizeAtUnitZ;
            imagePlaneSizeAtUnitZ.y = tan(verticalFov.radians / 2);
            imagePlaneSizeAtUnitZ.x = imagePlaneSizeAtUnitZ.y * aspectRatioWByH;

            float2 s = 1 / imagePlaneSizeAtUnitZ;

            // [ W 0 0 0 ]   [x]   [ Wx     ]
            // [ 0 H 0 0 ] * [y] = [ Hy     ]
            // [ 0 0 A B ]   [z]   [ Az + B ]
            // [ 0 0 1 0 ]   [1]   [  z     ]

            // (Az0 + B) / z0 == 0
            // (Az1 + B) / z1 == 1

            // -Az0 == B
            // (Az1 + B) / z1 == 1

            // Az1 - Az0 == z1
            // A(z1 - z0) == z1
            // A = z1 / (z1 - z0)

            float a = depth1Plane / (depth1Plane - depth0Plane);
            float b = -a * depth0Plane;

            // Flip Z signs because right handed view space
            // has -Z == front
            return Matrix {
                { s.x,   0,   0,   0 },
                {   0, s.y,   0,   0 },
                {   0,   0,  -a,  -b },
                {   0,   0,  -1,   0 },
            };
        }

        Matrix Matrix::projectionPerspective(uint2 resolution, Angle verticalFov, float depth1Plane, float depth0Plane)
        {
            auto fres = float2(resolution);
            return projectionPerspective(fres.x / fres.y, verticalFov, depth1Plane, depth0Plane);
        }

        Matrix Matrix::projectionOrtho(float width, float height, float depth1Plane, float depth0Plane)
        {
            return Matrix::projectionOrtho(float2(width, height), depth1Plane, depth0Plane);
        }

        Matrix Matrix::projectionOrtho(float2 dims, float depth1Plane, float depth0Plane)
        {
            // Right handed coordinates, so flip Z, but respect if the depths were already
            // given in right handed view space
            depth1Plane = -std::abs(depth1Plane);
            depth0Plane = -std::abs(depth0Plane);

            float2 s = 2.f / dims;

            // (Az0 + B) == 0
            // (Az1 + B) == 1

            // B = -Az0
            // Az1 - Az0 == 1
            // A == 1 / (z1 - z0) 

            float a = 1 / (depth1Plane - depth0Plane);
            float b = -a * depth0Plane;

            // float a = depth1Plane / (depth1Plane - depth0Plane);
            // float b = -a * depth0Plane;

            return Matrix {
                { s.x,   0,   0,   0 },
                {   0, s.y,   0,   0 },
                {   0,   0,   a,   b },
                {   0,   0,   0,   1 },
            };
        }

        Matrix Matrix::projectionJitter(float2 jitter)
        {
            return Matrix {
                { 0, 0, 0, jitter.x },
                { 0, 0, 0, jitter.y },
                { 0, 0, 0, 0        },
                { 0, 0, 0, 0        },
            };
        }

        Matrix Matrix::azimuthElevation(Angle azimuth, Angle elevation)
        {
            Matrix A = Matrix::axisAngle({ 0, 1, 0 }, azimuth);
            Matrix E = Matrix::axisAngle({ 1, 0, 0 }, elevation);
            return A * E;
        }

        // Determinant and inverse formulas from
        // http://www.euclideanspace.com/maths/algebra/matrix/functions/inverse/fourD/index.htm
        float Matrix::determinant() const
        {
            return 
                m(0, 3)*m(1, 2)*m(2, 1)*m(3, 0) - m(0, 2)*m(1, 3)*m(2, 1)*m(3, 0) - m(0, 3)*m(1, 1)*m(2, 2)*m(3, 0) + m(0, 1)*m(1, 3)*m(2, 2)*m(3, 0) +
                m(0, 2)*m(1, 1)*m(2, 3)*m(3, 0) - m(0, 1)*m(1, 2)*m(2, 3)*m(3, 0) - m(0, 3)*m(1, 2)*m(2, 0)*m(3, 1) + m(0, 2)*m(1, 3)*m(2, 0)*m(3, 1) +
                m(0, 3)*m(1, 0)*m(2, 2)*m(3, 1) - m(0, 0)*m(1, 3)*m(2, 2)*m(3, 1) - m(0, 2)*m(1, 0)*m(2, 3)*m(3, 1) + m(0, 0)*m(1, 2)*m(2, 3)*m(3, 1) +
                m(0, 3)*m(1, 1)*m(2, 0)*m(3, 2) - m(0, 1)*m(1, 3)*m(2, 0)*m(3, 2) - m(0, 3)*m(1, 0)*m(2, 1)*m(3, 2) + m(0, 0)*m(1, 3)*m(2, 1)*m(3, 2) +
                m(0, 1)*m(1, 0)*m(2, 3)*m(3, 2) - m(0, 0)*m(1, 1)*m(2, 3)*m(3, 2) - m(0, 2)*m(1, 1)*m(2, 0)*m(3, 3) + m(0, 1)*m(1, 2)*m(2, 0)*m(3, 3) +
                m(0, 2)*m(1, 0)*m(2, 1)*m(3, 3) - m(0, 0)*m(1, 2)*m(2, 1)*m(3, 3) - m(0, 1)*m(1, 0)*m(2, 2)*m(3, 3) + m(0, 0)*m(1, 1)*m(2, 2)*m(3, 3);
        }

        Matrix Matrix::inverse() const
        {
            Matrix m1 = Matrix::zero();
            m1(0, 0) = m(1, 2)*m(2, 3)*m(3, 1) - m(1, 3)*m(2, 2)*m(3, 1) + m(1, 3)*m(2, 1)*m(3, 2) - m(1, 1)*m(2, 3)*m(3, 2) - m(1, 2)*m(2, 1)*m(3, 3) + m(1, 1)*m(2, 2)*m(3, 3);
            m1(0, 1) = m(0, 3)*m(2, 2)*m(3, 1) - m(0, 2)*m(2, 3)*m(3, 1) - m(0, 3)*m(2, 1)*m(3, 2) + m(0, 1)*m(2, 3)*m(3, 2) + m(0, 2)*m(2, 1)*m(3, 3) - m(0, 1)*m(2, 2)*m(3, 3);
            m1(0, 2) = m(0, 2)*m(1, 3)*m(3, 1) - m(0, 3)*m(1, 2)*m(3, 1) + m(0, 3)*m(1, 1)*m(3, 2) - m(0, 1)*m(1, 3)*m(3, 2) - m(0, 2)*m(1, 1)*m(3, 3) + m(0, 1)*m(1, 2)*m(3, 3);
            m1(0, 3) = m(0, 3)*m(1, 2)*m(2, 1) - m(0, 2)*m(1, 3)*m(2, 1) - m(0, 3)*m(1, 1)*m(2, 2) + m(0, 1)*m(1, 3)*m(2, 2) + m(0, 2)*m(1, 1)*m(2, 3) - m(0, 1)*m(1, 2)*m(2, 3);
            m1(1, 0) = m(1, 3)*m(2, 2)*m(3, 0) - m(1, 2)*m(2, 3)*m(3, 0) - m(1, 3)*m(2, 0)*m(3, 2) + m(1, 0)*m(2, 3)*m(3, 2) + m(1, 2)*m(2, 0)*m(3, 3) - m(1, 0)*m(2, 2)*m(3, 3);
            m1(1, 1) = m(0, 2)*m(2, 3)*m(3, 0) - m(0, 3)*m(2, 2)*m(3, 0) + m(0, 3)*m(2, 0)*m(3, 2) - m(0, 0)*m(2, 3)*m(3, 2) - m(0, 2)*m(2, 0)*m(3, 3) + m(0, 0)*m(2, 2)*m(3, 3);
            m1(1, 2) = m(0, 3)*m(1, 2)*m(3, 0) - m(0, 2)*m(1, 3)*m(3, 0) - m(0, 3)*m(1, 0)*m(3, 2) + m(0, 0)*m(1, 3)*m(3, 2) + m(0, 2)*m(1, 0)*m(3, 3) - m(0, 0)*m(1, 2)*m(3, 3);
            m1(1, 3) = m(0, 2)*m(1, 3)*m(2, 0) - m(0, 3)*m(1, 2)*m(2, 0) + m(0, 3)*m(1, 0)*m(2, 2) - m(0, 0)*m(1, 3)*m(2, 2) - m(0, 2)*m(1, 0)*m(2, 3) + m(0, 0)*m(1, 2)*m(2, 3);
            m1(2, 0) = m(1, 1)*m(2, 3)*m(3, 0) - m(1, 3)*m(2, 1)*m(3, 0) + m(1, 3)*m(2, 0)*m(3, 1) - m(1, 0)*m(2, 3)*m(3, 1) - m(1, 1)*m(2, 0)*m(3, 3) + m(1, 0)*m(2, 1)*m(3, 3);
            m1(2, 1) = m(0, 3)*m(2, 1)*m(3, 0) - m(0, 1)*m(2, 3)*m(3, 0) - m(0, 3)*m(2, 0)*m(3, 1) + m(0, 0)*m(2, 3)*m(3, 1) + m(0, 1)*m(2, 0)*m(3, 3) - m(0, 0)*m(2, 1)*m(3, 3);
            m1(2, 2) = m(0, 1)*m(1, 3)*m(3, 0) - m(0, 3)*m(1, 1)*m(3, 0) + m(0, 3)*m(1, 0)*m(3, 1) - m(0, 0)*m(1, 3)*m(3, 1) - m(0, 1)*m(1, 0)*m(3, 3) + m(0, 0)*m(1, 1)*m(3, 3);
            m1(2, 3) = m(0, 3)*m(1, 1)*m(2, 0) - m(0, 1)*m(1, 3)*m(2, 0) - m(0, 3)*m(1, 0)*m(2, 1) + m(0, 0)*m(1, 3)*m(2, 1) + m(0, 1)*m(1, 0)*m(2, 3) - m(0, 0)*m(1, 1)*m(2, 3);
            m1(3, 0) = m(1, 2)*m(2, 1)*m(3, 0) - m(1, 1)*m(2, 2)*m(3, 0) - m(1, 2)*m(2, 0)*m(3, 1) + m(1, 0)*m(2, 2)*m(3, 1) + m(1, 1)*m(2, 0)*m(3, 2) - m(1, 0)*m(2, 1)*m(3, 2);
            m1(3, 1) = m(0, 1)*m(2, 2)*m(3, 0) - m(0, 2)*m(2, 1)*m(3, 0) + m(0, 2)*m(2, 0)*m(3, 1) - m(0, 0)*m(2, 2)*m(3, 1) - m(0, 1)*m(2, 0)*m(3, 2) + m(0, 0)*m(2, 1)*m(3, 2);
            m1(3, 2) = m(0, 2)*m(1, 1)*m(3, 0) - m(0, 1)*m(1, 2)*m(3, 0) - m(0, 2)*m(1, 0)*m(3, 1) + m(0, 0)*m(1, 2)*m(3, 1) + m(0, 1)*m(1, 0)*m(3, 2) - m(0, 0)*m(1, 1)*m(3, 2);
            m1(3, 3) = m(0, 1)*m(1, 2)*m(2, 0) - m(0, 2)*m(1, 1)*m(2, 0) + m(0, 2)*m(1, 0)*m(2, 1) - m(0, 0)*m(1, 2)*m(2, 1) - m(0, 1)*m(1, 0)*m(2, 2) + m(0, 0)*m(1, 1)*m(2, 2);
            m1 *= 1 / determinant();
            return m1;
        }
}

    String toString(const math::Matrix & m)
    {
        return String::format("{%s, %s, %s, %s}",
                              toString(m.row(0)),
                              toString(m.row(1)),
                              toString(m.row(2)),
                              toString(m.row(3)));
    }
}
