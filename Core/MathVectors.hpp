#pragma once

#include "OS.hpp"
#include "Utils.hpp"
#include "String.hpp"
#include "Hash.hpp"

#include <cstdint>

namespace Xor
{
    namespace math
    {
        using Xor::uint;

        static constexpr float Pi = 3.1415926535f;

        static constexpr int SwizzleDontCare = -1;
        static constexpr int SwizzleZero     = -2;
        static constexpr int SwizzleOne      = -3;

        static constexpr bool isSwizzleValid(int i, uint N)
        {
            return i >= SwizzleOne && i < static_cast<int>(N);
        }

        static constexpr bool isSwizzleAssignable(int i)
        {
            return i >= SwizzleDontCare;
        }

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
            constexpr VectorBase(T value)  : x(value), y(value), z(value), w(value) {}
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
            using Elem = T;
            static constexpr uint Dim = N;

            using VectorBase<T, N>::VectorBase;
            Vector() = default;

            template <typename U, uint M>
            explicit Vector(const Vector<U, M> &v)
            {
                uint n = std::min(N, M);
                for (uint i = 0; i < n; ++i)
                    (*this)[i] = T(v[i]);
            }

            Span<T> span() { return Span<T>(&x, N); }
            constexpr Span<const T> span() const { return Span<const T>(&x, N); }

            T *data() { return &this->x; }
            constexpr const T *data() const { return &this->x; }

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

            Vector &operator+=(Vector a) { *this = *this + a; return *this; }
            Vector &operator-=(Vector a) { *this = *this - a; return *this; }
            Vector &operator*=(Vector a) { *this = *this * a; return *this; }
            Vector &operator/=(Vector a) { *this = *this / a; return *this; }

            template <uint M>
            Vector<T, M> vec()  const { return Vector<T, M>(*this); }
            Vector<T, 2> vec2() const { return Vector<T, 2>(*this); }
            Vector<T, 3> vec3() const { return Vector<T, 3>(*this); }
            Vector<T, 4> vec4() const { return Vector<T, 4>(*this); }

            template <typename V, int SX, int SY = SwizzleDontCare, int SZ = SwizzleDontCare, int SW = SwizzleDontCare>
            class Swizzle
            {
                static constexpr uint Dim =
                    static_cast<uint>(SX != SwizzleDontCare) +
                    static_cast<uint>(SY != SwizzleDontCare) +
                    static_cast<uint>(SZ != SwizzleDontCare) +
                    static_cast<uint>(SW != SwizzleDontCare);

                using Vec = Vector<T, Dim>;

                V &v;

                __forceinline void lhs(int S, const Vec &a, int i)
                {
                    if (S != SwizzleDontCare && i < Dim)
                        v[S] = a[i];
                }

                __forceinline void rhs(Vec &c, int i, int S) const
                {
                    if (S != SwizzleDontCare && i < Dim)
                    {
                        if (S == SwizzleZero)
                            c[i] = 0;
                        else if (S == SwizzleOne)
                            c[i] = 1;
                        else
                            c[i] = v[S];
                    }
                }

            public:

                static_assert(isSwizzleValid(SX, N) &&
                              isSwizzleValid(SY, N) &&
                              isSwizzleValid(SZ, N) &&
                              isSwizzleValid(SW, N),
                              "Invalid swizzle components for vector");

                explicit Swizzle(V  &v) : v(v) {}
                explicit Swizzle(V &&v) : v(v) {}

                operator Vec() const
                {
                    Vec c;
                    rhs(c, 0, SX);
                    rhs(c, 1, SY);
                    rhs(c, 2, SZ);
                    rhs(c, 3, SW);
                    return c;
                }

                Swizzle &operator=(Vec a)
                {
                    lhs(SX, a, 0);
                    lhs(SY, a, 1);
                    lhs(SZ, a, 2);
                    lhs(SW, a, 3);
                    return *this;
                }

                Swizzle &operator=(const Swizzle &s)
                {
                    return operator=(static_cast<Vec>(s));
                }

                Swizzle &operator+=(Vec a) { *this = Vec(*this) + a; return *this; }
                Swizzle &operator-=(Vec a) { *this = Vec(*this) - a; return *this; }
                Swizzle &operator*=(Vec a) { *this = Vec(*this) * a; return *this; }
                Swizzle &operator/=(Vec a) { *this = Vec(*this) / a; return *this; }

                template <uint M>
                Vector<T, M> vec()  const { return Vector<T, M>(Vec(*this)); }
                Vector<T, 2> vec2() const { return Vector<T, 2>(Vec(*this)); }
                Vector<T, 3> vec3() const { return Vector<T, 3>(Vec(*this)); }
                Vector<T, 4> vec4() const { return Vector<T, 4>(Vec(*this)); }
            };

            template <int SX, int SY = SwizzleDontCare, int SZ = SwizzleDontCare, int SW = SwizzleDontCare>
            auto swizzle()
            {
                return Swizzle<Vector, SX, SY, SZ, SW>(*this);
            }

            template <int SX, int SY = SwizzleDontCare, int SZ = SwizzleDontCare, int SW = SwizzleDontCare>
            auto swizzle() const
            {
                return Swizzle<const Vector, SX, SY, SZ, SW>(*this);
            }

            T lengthSqr() const
            {
                return dot(*this, *this);
            }

            float length() const
            {
                return std::sqrt(lengthSqr());
            }
        };

#include "MathVectorSwizzle.hpp"

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

        template <typename T, uint N>
        inline T dot(Vector<T, N> a, Vector<T, N> b)
        {
            T dp = 0;
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

        template <uint N>
        inline Vector<float, N> fmod(Vector<float, N> a, Vector<float, N> b)
        {
            Vector<float, N> c;
            for (uint i = 0; i < N; ++i) c[i] = fmodf(a[i], b[i]);
            return c;
        }

        template <uint N>
        inline Vector<float, N> sqrtVec(Vector<float, N> a)
        {
            Vector<float, N> c;
            for (uint i = 0; i < N; ++i) c[i] = std::sqrt(a[i]);
            return c;
        }

        template <typename T, uint N>
        Vector<T, N> abs(Vector<T, N> a)
        {
            Vector<T, N> c;
            for (uint i = 0; i < N; ++i) c[i] = std::abs(a[i]);
            return c;
        }

        template <typename T, uint N>
        Vector<T, N> min(Vector<T, N> a, Vector<T, N> b)
        {
            Vector<T, N> c;
            for (uint i = 0; i < N; ++i) c[i] = std::min(a[i], b[i]);
            return c;
        }

        template <typename T, uint N>
        Vector<T, N> max(Vector<T, N> a, Vector<T, N> b)
        {
            Vector<T, N> c;
            for (uint i = 0; i < N; ++i) c[i] = std::max(a[i], b[i]);
            return c;
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

        template <typename T, uint N>
        inline Vector<T, N> clamp(Vector<T, N> x, Vector<T, N> minimum, Vector<T, N> maximum)
        {
            return max(minimum, min(maximum, x));
        }

        template <typename T, uint N>
        inline T smallestElement(Vector<T, N> a)
        {
            T smallest = a.x;

            for (uint i = 1; i < N; ++i)
                smallest = std::min(smallest, a[i]);

            return smallest;
        }

        template <typename T, uint N>
        inline T largestElement(Vector<T, N> a)
        {
            T largest = a.x;

            for (uint i = 1; i < N; ++i)
                largest = std::max(largest, a[i]);

            return largest;
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

        struct AxisAngleRotation
        {
            float3 axis    = float3(1, 0, 0);
            float cosAngle = 1;
            float sinAngle = 0;

            AxisAngleRotation() = default;
            AxisAngleRotation(float3 axis, float cosAngle, float sinAngle)
                : axis(axis)
                , cosAngle(cosAngle)
                , sinAngle(sinAngle)
            {}
            AxisAngleRotation(float3 axis, Angle angle);

            static AxisAngleRotation fromTo(float3 aUnit, float3 bUnit);

            float3 rotate(float3 v) const;
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

            Matrix(Span<const float> values)
            {
                XOR_ASSERT(values.size() == 16, "Must give all elements of the matrix in row-major order");
                memcpy(m_rows, values.data(), sizeof(m_rows));
            }

            Matrix(const float (&values)[4][4])
            {
                memcpy(m_rows, values, sizeof(m_rows));
            }

            Matrix(const float (&values)[16])
            {
                memcpy(m_rows, values, sizeof(m_rows));
            }

            Matrix(float m11, float m12, float m13, float m14,
                   float m21, float m22, float m23, float m24,
                   float m31, float m32, float m33, float m34,
                   float m41, float m42, float m43, float m44)
                : Matrix({m11, m12, m13, m14, 
                          m21, m22, m23, m24,
                          m31, m32, m33, m34,
                          m41, m42, m43, m44, })
            {}

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
            static Matrix axisAngle(float3 axis, float cosAngle, float sinAngle);
            static Matrix axisAngle(float3 axis, Angle angle);
            static Matrix rotateFromTo(float3 aUnit, float3 bUnit);

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
            static Matrix projectionOrtho(float width,
                                          float height,
                                          float depth1Plane = DefaultDepth1Plane,
                                          float depth0Plane = DefaultDepth0Plane);
            static Matrix projectionOrtho(float2 dims,
                                          float depth1Plane = DefaultDepth1Plane,
                                          float depth0Plane = DefaultDepth0Plane);
            static Matrix projectionJitter(float2 jitter);

            static Matrix azimuthElevation(Angle azimuth, Angle elevation);
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

        // Matrix of arbitrary dimensions. Less functionality, more generality.
        // Amount of rows comes first to conform with M-by-N notation.
        template <typename T, uint M, uint N>
        class Mat
        {
            T v[M][N];
        public:
            Mat()
            {
                zero(v);
            }

            template <typename... Ts> Mat(T v0, T v1, const Ts &... values)
            {
                // Put two fixed T parameters in the constructor so it doesn't mess up overloads for
                // one-argument constructors.
                static_assert(sizeof...(Ts) == N * M - 2, "Must give all elements of the matrix in row-major order.");
                T fs[N * M] = { v0, v1, values... };
                static_assert(sizeof(v) == sizeof(fs), "Unexpected size mismatch");
                memcpy(v, fs, sizeof(v));
            }

            Mat(Span<const T> values)
            {
                XOR_ASSERT(values.size() == N * M, "Must give all elements of the matrix in row-major order");
                memcpy(v, values.data(), sizeof(v));
            }

            Mat(const T (&values)[M][N])
            {
                static_assert(sizeof(v) == sizeof(values), "Unexpected size mismatch");
                memcpy(v, values, sizeof(v));
            }

            Mat(const T (&values)[M * N])
            {
                static_assert(sizeof(v) == sizeof(values), "Unexpected size mismatch");
                memcpy(v, values, sizeof(v));
            }

            T &m(uint y, uint x) { return v[y][x]; }
            T m(uint y, uint x) const { return v[y][x]; }
            T &operator()(uint y, uint x) { return v[y][x]; }
            T operator()(uint y, uint x) const { return v[y][x]; }

            Mat<T, N, M> transpose() const
            {
                T fs[N][M];

                for (uint x = 0; x < N; ++x)
                {
                    for (uint y = 0; y < M; ++y)
                    {
                        fs[x][y] = m(x, y);
                    }
                }

                return fs;
            }

            T determinant() const
            {
                XOR_ASSERT(N == M, "Determinant is only defined for square matrices");

                if (N == 2)
                {
                    return m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0);
                }
                else if (N == 3)
                {
                    return
                        m(0, 0) * m(1, 1) * m(2, 2) +
                        m(0, 1) * m(1, 2) * m(2, 0) +
                        m(0, 2) * m(1, 0) * m(2, 1) -
                        m(0, 2) * m(1, 1) * m(2, 0) -
                        m(0, 1) * m(1, 0) * m(2, 2) -
                        m(0, 0) * m(1, 2) * m(2, 1);
                }
                else if (N == 4)
                {
                    return
                        m(0, 3)*m(1, 2)*m(2, 1)*m(3, 0) - m(0, 2)*m(1, 3)*m(2, 1)*m(3, 0) - m(0, 3)*m(1, 1)*m(2, 2)*m(3, 0) + m(0, 1)*m(1, 3)*m(2, 2)*m(3, 0) +
                        m(0, 2)*m(1, 1)*m(2, 3)*m(3, 0) - m(0, 1)*m(1, 2)*m(2, 3)*m(3, 0) - m(0, 3)*m(1, 2)*m(2, 0)*m(3, 1) + m(0, 2)*m(1, 3)*m(2, 0)*m(3, 1) +
                        m(0, 3)*m(1, 0)*m(2, 2)*m(3, 1) - m(0, 0)*m(1, 3)*m(2, 2)*m(3, 1) - m(0, 2)*m(1, 0)*m(2, 3)*m(3, 1) + m(0, 0)*m(1, 2)*m(2, 3)*m(3, 1) +
                        m(0, 3)*m(1, 1)*m(2, 0)*m(3, 2) - m(0, 1)*m(1, 3)*m(2, 0)*m(3, 2) - m(0, 3)*m(1, 0)*m(2, 1)*m(3, 2) + m(0, 0)*m(1, 3)*m(2, 1)*m(3, 2) +
                        m(0, 1)*m(1, 0)*m(2, 3)*m(3, 2) - m(0, 0)*m(1, 1)*m(2, 3)*m(3, 2) - m(0, 2)*m(1, 1)*m(2, 0)*m(3, 3) + m(0, 1)*m(1, 2)*m(2, 0)*m(3, 3) +
                        m(0, 2)*m(1, 0)*m(2, 1)*m(3, 3) - m(0, 0)*m(1, 2)*m(2, 1)*m(3, 3) - m(0, 1)*m(1, 0)*m(2, 2)*m(3, 3) + m(0, 0)*m(1, 1)*m(2, 2)*m(3, 3);
                }
                else
                {
                    XOR_CHECK(false, "Determinant not implemented for N >= 5");
                    __assume(0);
                }
            }

            template <uint N, uint M, uint K>
            friend inline Mat<T, M, K> operator*(const Mat<T, M, N> &a, const Mat<T, N, K> &b)
            {
                T m[M][K] = { 0 };

                for (uint y = 0; y < M; ++y)
                {
                    for (uint x = 0; x < K; ++x)
                    {
                        for (uint i = 0; i < N; ++i)
                            m[y][x] += a(y, i) * b(i, x);
                    }
                }

                return m;
            }

            friend inline Mat operator+(const Mat &a, const Mat &b)
            {
                T m[M][N];

                for (uint y = 0; y < M; ++y)
                {
                    for (uint x = 0; x < N; ++x)
                    {
                        m[y][x] = a(y, x) + b(y, x);
                    }
                }

                return m;
            }

            Mat &operator*=(T k)
            {
                for (uint y = 0; y < M; ++y)
                {
                    for (uint x = 0; x < N; ++x)
                        m(y, x) *= k;
                }
                return *this;
            }

            friend inline Mat operator*(const Mat &a, T k)
            {
                Mat m = a;
                m *= k;
                return m;
            }

            friend inline Mat operator*(T k, const Mat &a)
            {
                return a * k;
            }
        };

        using float4x4 = Matrix;
        using float3x3 = Mat<float, 3, 3>;
        using int4x4 = Mat<int, 4, 4>;
        using int3x3 = Mat<int, 3, 3>;

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
#if !defined(XOR_INTELLISENSE)
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
#endif
    }

    using Xor::math::Pi;
    using Xor::math::Vector;
    using Xor::math::Angle;
    using Xor::math::AxisAngleRotation;
    using Xor::math::Matrix;
    using Xor::math::Mat;

    template <typename T, uint N> String toString(Vector<T, N> v)
    {
        String elems[N];

        for (uint i = 0; i < N; ++i)
            elems[i] = toString(v[i]);

        return String::format("{ %s }", String::join(elems, ", ").cStr());
    }

    String toString(const Matrix &m);

    namespace math_detail
    {
        template <typename T>
        __forceinline void compareAndSwap(T &a, T &b)
        {
            if (b < a)
                std::swap(a, b);
        }
    }

    template <typename T, uint N>
    int indexOf(Vector<T, N> v, T value)
    {
        for (int i = 0; i < static_cast<int>(N); ++i)
        {
            if (value == v[i])
                return i;
        }

        return -1;
    }

    // Sort vectors using optimal sorting networks
    template <typename T>
    Vector<T, 2> sortVector(Vector<T, 2> v)
    {
        math_detail::compareAndSwap(v.x, v.y);
        return v;
    }

    template <typename T>
    Vector<T, 3> sortVector(Vector<T, 3> v)
    {
        math_detail::compareAndSwap(v.x, v.y);
        math_detail::compareAndSwap(v.x, v.z);
        math_detail::compareAndSwap(v.y, v.z);
        return v;
    }

    template <typename T>
    Vector<T, 4> sortVector(Vector<T, 4> v)
    {
        math_detail::compareAndSwap(v.x, v.y);
        math_detail::compareAndSwap(v.z, v.w);
        math_detail::compareAndSwap(v.x, v.z);
        math_detail::compareAndSwap(v.y, v.w);
        math_detail::compareAndSwap(v.y, v.z);
        return v;
    }
}

using Xor::math::int2;
using Xor::math::int3;
using Xor::math::int4;
using Xor::math::uint;
using Xor::math::uint2;
using Xor::math::uint3;
using Xor::math::uint4;
using Xor::math::float2;
using Xor::math::float3;
using Xor::math::float4;
using Xor::math::float4x4;
using Xor::math::float3x3;
using Xor::math::int4x4;
using Xor::math::int3x3;
