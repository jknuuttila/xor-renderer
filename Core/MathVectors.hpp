#pragma once

namespace xor
{
    namespace math
    {
        template <typename T, unsigned N> struct VectorBase;

        template <typename T>
        struct VectorBase<T, 2>
        {
            T x = 0;
            T y = 0;

            VectorBase() = default;
            VectorBase(T value)  : x(value), y(value) {}
            VectorBase(T x, T y) : x(x), y(y) {}
        };
        template <typename T>
        struct VectorBase<T, 3>
        {
            T x = 0;
            T y = 0;
            T z = 0;

            VectorBase() = default;
            VectorBase(T value)  : x(value), y(value), z(value) {}
            VectorBase(T x, T y, T z = 0) : x(x), y(y), z(z) {}
        };
        template <typename T>
        struct VectorBase<T, 4>
        {
            T x = 0;
            T y = 0;
            T z = 0;
            T w = 0;

            VectorBase() = default;
            VectorBase(T value)  : x(value), y(value), z(value) {}
            VectorBase(T x, T y, T z = 0, T w = 0) : x(x), y(y), z(z), w(w) {}
        };

        template <typename T, unsigned N>
        struct Vector : public VectorBase<T, N>
        {
            static_assert(sizeof(Vector) == sizeof(T) * N,
                          "Unexpected padding inside Vector.");

            using VectorBase<T, N>::VectorBase;

        };
    }
}
