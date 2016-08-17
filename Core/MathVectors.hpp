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
        };

        template <uint N>
        bool all(const Vector<bool, N> &a)
        {
            bool b = true;
            for (uint i = 0; i < N; ++i) b = b && a[i];
            return b;
        }

        template <uint N>
        bool any(const Vector<bool, N> &a)
        {
            bool b = false;
            for (uint i = 0; i < N; ++i) b = b || a[i];
            return b;
        }

        template <uint N>
        Vector<bool, N> operator&&(const Vector<bool, N> &a, const Vector<bool, N> &b)
        {
            Vector<bool, N> c;
            for (uint i = 0; i < N; ++i) c[i] = a[i] && b[i];
            return c;
        }

        template <uint N>
        Vector<bool, N> operator||(const Vector<bool, N> &a, const Vector<bool, N> &b)
        {
            Vector<bool, N> c;
            for (uint i = 0; i < N; ++i) c[i] = a[i] || b[i];
            return c;
        }

        using int2   = Vector<int, 2>;
        using int3   = Vector<int, 3>;
        using int4   = Vector<int, 4>;
        using uint2  = Vector<uint, 2>;
        using uint3  = Vector<uint, 3>;
        using uint4  = Vector<uint, 4>;
        using float2 = Vector<float, 2>;
        using float3 = Vector<float, 3>;
        using float4 = Vector<float, 4>;

        static_assert(sizeof(int2) == sizeof(int) * 2, "Unexpected padding inside Vector.");
        static_assert(sizeof(int3) == sizeof(int) * 3, "Unexpected padding inside Vector.");
        static_assert(sizeof(int4) == sizeof(int) * 4, "Unexpected padding inside Vector.");
        static_assert(sizeof(uint2) == sizeof(uint) * 2, "Unexpected padding inside Vector.");
        static_assert(sizeof(uint3) == sizeof(uint) * 3, "Unexpected padding inside Vector.");
        static_assert(sizeof(uint4) == sizeof(uint) * 4, "Unexpected padding inside Vector.");
        static_assert(sizeof(float2) == sizeof(float) * 2, "Unexpected padding inside Vector.");
        static_assert(sizeof(float3) == sizeof(float) * 3, "Unexpected padding inside Vector.");
        static_assert(sizeof(float4) == sizeof(float) * 4, "Unexpected padding inside Vector.");
        static_assert(std::is_trivially_copyable<int2>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<int3>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<int4>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<uint2>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<uint3>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<uint4>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<float2>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<float3>::value, "Unexpectedly non-POD.");
        static_assert(std::is_trivially_copyable<float4>::value, "Unexpectedly non-POD.");
    }

    template <typename T, uint N> String toString(const math::Vector<T, N> &v)
    {
        String elems[N];

        for (uint i = 0; i < N; ++i)
            elems[i] = toString(v[i]);

        return String::format("{ %s }", String::join(elems, ", ").cStr());
    }
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
