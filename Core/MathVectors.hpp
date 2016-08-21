#pragma once

#include "OS.hpp"
#include "Utils.hpp"
#include "String.hpp"

#include <cstdint>

namespace xor
{
    namespace math
    {
        using xor::uint;

        static const float Pi = 3.1415926535f;

        template <typename T, uint N> struct VectorBase;

        template <typename T>
        struct VectorBase<T, 2>
        {
            T x = 0;
            T y = 0;

            constexpr VectorBase() = default;
            constexpr VectorBase(T value)  : x(value), y(value) {}
            constexpr VectorBase(T x, T y) : x(x), y(y) {}

            template <typename U>
            constexpr explicit VectorBase(const VectorBase<U, 2> &v)
                : x(static_cast<T>(v.x))
                , y(static_cast<T>(v.y))
            {}
        };
        template <typename T>
        struct VectorBase<T, 3>
        {
            T x = 0;
            T y = 0;
            T z = 0;

            constexpr VectorBase() = default;
            constexpr VectorBase(T value)  : x(value), y(value), z(value) {}
            constexpr VectorBase(T x, T y, T z = 0) : x(x), y(y), z(z) {}

            template <typename U>
            constexpr explicit VectorBase(const VectorBase<U, 3> &v)
                : x(static_cast<T>(v.x))
                , y(static_cast<T>(v.y))
                , z(static_cast<T>(v.z))
            {}
        };

        template <typename T>
        struct VectorBase<T, 4>
        {
            T x = 0;
            T y = 0;
            T z = 0;
            T w = 0;

            constexpr VectorBase() = default;
            constexpr VectorBase(T value)  : x(value), y(value), z(value) {}
            constexpr VectorBase(T x, T y, T z = 0, T w = 0) : x(x), y(y), z(z), w(w) {}

            template <typename U>
            constexpr explicit VectorBase(const VectorBase<U, 4> &v)
                : x(static_cast<T>(v.x))
                , y(static_cast<T>(v.y))
                , z(static_cast<T>(v.z))
                , w(static_cast<T>(v.w))
            {}
        };

        template <typename T, uint N>
        struct Vector : public VectorBase<T, N>
        {
            using VectorBase<T, N>::VectorBase;
            Vector() = default;

            template <uint M>
            explicit Vector(const Vector<T, M> &v)
            {
                uint n = std::min(N, M);
                for (uint i = 0; i < n; ++i)
                    (*this)[i] = v[i];
            }

            Span<T> span() { return Span<T>(&x, N); }
            constexpr Span<const T> span() const { return Span<const T>(&x, N); }

            T *data() { return &x; }
            constexpr const T *data() const { return &x; }

            T &operator[](uint i) { return data()[i]; }
            const T &operator[](uint i) const { return data()[i]; }

            Vector operator-() const
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = -data()[i];
                return c;
            }

            Vector<bool, N> operator!() const
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = !data()[i];
                return c;
            }

            friend Vector operator+(Vector a, Vector b)
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] + b[i];
                return c;
            }

            friend Vector operator-(Vector a, Vector b)
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] - b[i];
                return c;
            }

            friend Vector operator*(Vector a, Vector b)
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] * b[i];
                return c;
            }

            friend Vector operator/(Vector a, Vector b)
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] / b[i];
                return c;
            }

            friend Vector operator%(Vector a, Vector b)
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] % b[i];
                return c;
            }

            friend Vector<bool, N> operator==(Vector a, Vector b)
            {
                Vector<bool, N> c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] == b[i];
                return c;
            }

            friend Vector<bool, N> operator!=(Vector a, Vector b)
            {
                Vector<bool, N> c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] != b[i];
                return c;
            }

            friend Vector<bool, N> operator<(Vector a, Vector b)
            {
                Vector<bool, N> c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] < b[i];
                return c;
            }

            friend Vector<bool, N> operator>(Vector a, Vector b)
            {
                Vector<bool, N> c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] > b[i];
                return c;
            }

            friend Vector<bool, N> operator<=(Vector a, Vector b)
            {
                Vector<bool, N> c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] <= b[i];
                return c;
            }

            friend Vector<bool, N> operator>=(Vector a, Vector b)
            {
                Vector<bool, N> c;
                for (uint i = 0; i < N; ++i) c[i] = a[i] >= b[i];
                return c;
            }

            friend Vector min(Vector a, Vector b)
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = std::min(a[i], b[i]);
                return c;
            }

            friend Vector max(Vector a, Vector b)
            {
                Vector c;
                for (uint i = 0; i < N; ++i) c[i] = std::max(a[i], b[i]);
                return c;
            }

            Vector &operator+=(Vector a) { *this = *this + a; return *this; }
            Vector &operator-=(Vector a) { *this = *this - a; return *this; }
            Vector &operator*=(Vector a) { *this = *this * a; return *this; }
            Vector &operator/=(Vector a) { *this = *this / a; return *this; }
        };

        using int2   = Vector<int, 2>;
        using int3   = Vector<int, 3>;
        using int4   = Vector<int, 4>;
        using uint2  = Vector<uint, 2>;
        using uint3  = Vector<uint, 3>;
        using uint4  = Vector<uint, 4>;
        using float2 = Vector<float, 2>;
        using float3 = Vector<float, 3>;
        using float4 = Vector<float, 4>;
        using bool2 = Vector<bool, 2>;
        using bool3 = Vector<bool, 3>;
        using bool4 = Vector<bool, 4>;

        template <uint N>
        inline bool all(Vector<bool, N> a)
        {
            bool b = true;
            for (uint i = 0; i < N; ++i) b = b && a[i];
            return b;
        }

        template <uint N>
        inline bool any(Vector<bool, N> a)
        {
            bool b = false;
            for (uint i = 0; i < N; ++i) b = b || a[i];
            return b;
        }

        template <uint N>
        inline Vector<bool, N> operator&&(Vector<bool, N> a, Vector<bool, N> b)
        {
            Vector<bool, N> c;
            for (uint i = 0; i < N; ++i) c[i] = a[i] && b[i];
            return c;
        }

        template <uint N>
        inline Vector<bool, N> operator||(Vector<bool, N> a, Vector<bool, N> b)
        {
            Vector<bool, N> c;
            for (uint i = 0; i < N; ++i) c[i] = a[i] || b[i];
            return c;
        }

        template <uint N>
        inline float dot(Vector<float, N> a, Vector<float, N> b)
        {
            float dp = 0;
            for (uint i = 0; i < N; ++i) dp += a[i] * b[i];
            return dp;
        }

        template <uint N>
        inline float lengthSqr(Vector<float, N> a)
        {
            return dot(a, a);
        }

        template <uint N>
        inline float length(Vector<float, N> a)
        {
            return sqrt(lengthSqr(a));
        }

        template <uint N>
        inline Vector<float, N> normalize(Vector<float, N> a)
        {
            return a / length(a);
        }

        template <uint N>
        inline Vector<float, N> normalize(Vector<float, N> a, Vector<float, N> defaultForZeroLength)
        {
            static const float Epsilon = .001f;
            float L = length(a);
            if (L < Epsilon)
                return defaultForZeroLength;
            else
                return a / L;
        }

        inline float3 cross(float3 a, float3 b)
        {
            // XYZZY
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x,
            };
        }

        static const float RadToDeg = 180.f / Pi;
        static const float DegToRad = Pi / 180.f;

        struct Angle
        {
            float radians = 0;

            Angle() = default;
            explicit Angle(float rad) : radians(rad) {}

            static Angle degrees(float deg)
            {
                return Angle(deg * DegToRad);
            }

            float toDeg() const { return radians * RadToDeg; }
        };

        static const Angle DefaultFov = Angle::degrees(60.f);
        static const float DefaultDepth0Plane = 100.f;
        static const float DefaultDepth1Plane =    .1f;

        class Matrix
        {
            float4 m_rows[4];

            struct ZeroMatrix {};
            Matrix(ZeroMatrix) {}
        public:
            Matrix()
            {
                *this = identity();
            }

            Matrix(float4 r0, float4 r1, float4 r2, float4 r3)
            {
                m_rows[0] = r0;
                m_rows[1] = r1;
                m_rows[2] = r2;
                m_rows[3] = r3;
            }

            explicit Matrix(Span<const float4> rows)
            {
                auto n = std::min(rows.size(), 4ull);
                for (uint i = 0; i < n; ++i)
                    m_rows[i] = rows[i];
            }

            static Matrix zero()
            {
                return Matrix(ZeroMatrix());
            }

            static Matrix identity()
            {
                return Matrix {
                    {1, 0, 0, 0},
                    {0, 1, 0, 0},
                    {0, 0, 1, 0},
                    {0, 0, 0, 1},
                };
            }

            float4 &row(uint r) { return m_rows[r]; }
            float4 row(uint r) const { return m_rows[r]; }

            float &m(uint y, uint x) { return row(y)[x]; }
            float m(uint y, uint x) const { return row(y)[x]; }
            float &operator()(uint y, uint x) { return row(y)[x]; }
            float operator()(uint y, uint x) const { return row(y)[x]; }

            inline float4 transform(float4 v) const;
            float4 transform(float3 v) const
            {
                return transform(float4(v.x, v.y, v.z, 1));
            }
            float3 transformAndProject(float3 v) const
            {
                float4 v_ = transform(v);
                return float3(v_ / v_.w);
            }

            static Matrix translation(float3 t)
            {
                return Matrix {
                    {1, 0, 0, t.x},
                    {0, 1, 0, t.y},
                    {0, 0, 1, t.z},
                    {0, 0, 0,   1},
                };
            }

            float3 getRotationXAxis() const { return { m(0, 0), m(1, 0), m(2, 0) }; }
            float3 getRotationYAxis() const { return { m(0, 1), m(1, 1), m(2, 1) }; }
            float3 getRotationZAxis() const { return { m(0, 2), m(1, 2), m(2, 2) }; }
            float3 getTranslation()   const { return { m(0, 3), m(1, 3), m(2, 3) }; }

            void setTranslation(float3 t)
            {
                m(0, 3) = t.x;
                m(1, 3) = t.y;
                m(2, 3) = t.z;
            }

            Matrix transpose() const
            {
                return Matrix {
                    {row(0)[0], row(1)[0], row(2)[0], row(3)[0]},
                    {row(0)[1], row(1)[1], row(2)[1], row(3)[1]},
                    {row(0)[2], row(1)[2], row(2)[2], row(3)[2]},
                    {row(0)[3], row(1)[3], row(2)[3], row(3)[3]},
                };
            }

            float determinant() const;
            Matrix inverse() const;

            friend inline Matrix operator*(const Matrix &a, const Matrix &b)
            {
                Matrix m(Matrix::ZeroMatrix {});

                for (uint y = 0; y < 4; ++y)
                {
                    for (uint x = 0; x < 4; ++x)
                    {
                        for (uint i = 0; i < 4; ++i)
                            m(y, x) += a.row(y)[i] * b.row(i)[x];
                    }
                }

                return m;
            }

            friend inline Matrix operator+(const Matrix &a, const Matrix &b)
            {
                Matrix m(Matrix::ZeroMatrix {});

                for (uint y = 0; y < 4; ++y)
                {
                    for (uint x = 0; x < 4; ++x)
                    {
                        m(y, x) = a(y, x) + b(y, x);
                    }
                }

                return m;
            }

            Matrix &operator*=(const Matrix &b)
            {
                Matrix m = (*this) * b;
                *this = m;
                return *this;
            }

            Matrix &operator*=(float k)
            {
                for (uint y = 0 ; y < 4; ++y)
                {
                    for (uint x = 0; x < 4; ++x)
                        m(y, x) *= k;
                }
                return *this;
            }

            friend inline Matrix operator*(const Matrix &a, float k)
            {
                Matrix m = a;
                m *= k;
                return m;
            }
            friend inline Matrix operator*(float k, const Matrix &a)
            {
                return a * k;
            }

            static Matrix crossProductMatrix(float3 k);
            static Matrix axisAngle(float3 axis, Angle angle);

            static Matrix lookInDirection(float3 dir,       float3 up = float3(0, 1, 0));
            static Matrix lookTo(float3 pos, float3 dir,    float3 up = float3(0, 1, 0));
            static Matrix lookAt(float3 pos, float3 target, float3 up = float3(0, 1, 0));

            static Matrix projectionPerspective(float aspectRatioWByH,
                                                Angle verticalFov = DefaultFov,
                                                float depth1Plane = DefaultDepth1Plane,
                                                float depth0Plane = DefaultDepth0Plane);
            static Matrix projectionPerspective(uint2 resolution,
                                                Angle verticalFov = DefaultFov,
                                                float depth1Plane = DefaultDepth1Plane,
                                                float depth0Plane = DefaultDepth0Plane);
        };

        inline float4 operator*(const Matrix &m, float4 v)
        {
            return
            {
                dot(m.row(0), v),
                dot(m.row(1), v),
                dot(m.row(2), v),
                dot(m.row(3), v),
            };
        }

        inline float4 Matrix::transform(float4 v) const
        {
            return (*this) * v;
        }

        using float4x4 = Matrix;

        static_assert(sizeof(int2) == sizeof(int) * 2, "Unexpected padding inside Vector.");
        static_assert(sizeof(int3) == sizeof(int) * 3, "Unexpected padding inside Vector.");
        static_assert(sizeof(int4) == sizeof(int) * 4, "Unexpected padding inside Vector.");
        static_assert(sizeof(uint2) == sizeof(uint) * 2, "Unexpected padding inside Vector.");
        static_assert(sizeof(uint3) == sizeof(uint) * 3, "Unexpected padding inside Vector.");
        static_assert(sizeof(uint4) == sizeof(uint) * 4, "Unexpected padding inside Vector.");
        static_assert(sizeof(float2) == sizeof(float) * 2, "Unexpected padding inside Vector.");
        static_assert(sizeof(float3) == sizeof(float) * 3, "Unexpected padding inside Vector.");
        static_assert(sizeof(float4) == sizeof(float) * 4, "Unexpected padding inside Vector.");
        static_assert(sizeof(Matrix) == sizeof(float4) * 4, "Unexpected padding inside Matrix.");
        static_assert(std::is_trivially_copyable<int2>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<int3>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<int4>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<uint2>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<uint3>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<uint4>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<float2>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<float3>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<float4>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<Matrix>::value, "Unexpectedly non-POD.");
    }

    using xor::math::Pi;
    using xor::math::Vector;
    using xor::math::Angle;
    using xor::math::Matrix;

    template <typename T, uint N> String toString(Vector<T, N> v)
    {
        String elems[N];

        for (uint i = 0; i < N; ++i)
            elems[i] = toString(v[i]);

        return String::format("{ %s }", String::join(elems, ", ").cStr());
    }

    String toString(const Matrix &m);
}

using xor::math::int2;
using xor::math::int3;
using xor::math::int4;
using xor::math::uint;
using xor::math::uint2;
using xor::math::uint3;
using xor::math::uint4;
using xor::math::float2;
using xor::math::float3;
using xor::math::float4;
using xor::math::float4x4;
