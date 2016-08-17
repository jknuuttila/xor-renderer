#include "Core/Core.hpp"

using namespace xor;
using xor::math::Vector;
using xor::math::Matrix;

template <typename T> struct Epsilon { static double value() { return 0; } };
template <> struct Epsilon<float> { static double value() { return 0.001; } };
template <> struct Epsilon<double> { static double value() { return 0.001; } };
template <typename T, uint N> struct Epsilon<Vector<T, N>> { static double value() { return Epsilon<T>::value(); } };

template <typename T> bool compareEq(const T &a, const T &b, double epsilon)
{
    bool plus  = a + static_cast<T>(epsilon) >= b;
    bool minus = a - static_cast<T>(epsilon) <= b;
    bool close = plus && minus;
    return close;
}

template <typename T, uint N> bool compareEq(const Vector<T, N> &a, const Vector<T, N> &b, double epsilon)
{
    for (uint i = 0; i < N; ++i)
    {
        if (!compareEq(a[i], b[i], epsilon))
            return false;
    }
    return true;
}

template <typename T>
bool checkEqImpl(const char *file, int line, const T &a, const T &b, double epsilon = Epsilon<T>::value())
{
    if (!compareEq(a, b, epsilon))
    {
        auto msg = String::format("Values don't match: %s != %s",
                                  toString(a).cStr(),
                                  toString(b).cStr());
        print("%s(%d): %s\n", file, line, msg.cStr());
        return false;
    }
    else
    {
        return true;
    }
}

#define XOR_CHECK_EQ(a, b, ...) XOR_DEBUG_BREAK_IF_FALSE(checkEqImpl(__FILE__, __LINE__, a, b, ## __VA_ARGS__))

void testTransformMatrices()
{
    {
        Matrix M  = Matrix::translation({1, 1, 1});
        float3 v  = { 1, 0, 0 };
        float3 v_ = M.transform(v);
        XOR_CHECK_EQ(v_, { 2, 1, 1 });
    }

    {
        Matrix M  = Matrix::lookToDirection({1, 0, 0}, {0, 1, 0});
        XOR_CHECK_EQ(M.transform(float3( 0,  0,  1)), { 1,  0,  0});
        XOR_CHECK_EQ(M.transform(float3( 1,  0,  0)), { 0,  0, -1});
        XOR_CHECK_EQ(M.transform(float3( 0,  1,  0)), { 0,  1,  0});
        XOR_CHECK_EQ(M.transform(float3( 0,  0, -1)), {-1,  0,  0});
        XOR_CHECK_EQ(M.transform(float3(-1,  0,  0)), { 0,  0,  1});
        XOR_CHECK_EQ(M.transform(float3( 0, -1,  0)), { 0, -1,  0});
    }

    {
        Matrix M  = Matrix::lookAt({1, 0, 0}, {0, 0, 0}, {0, 1, 0});
        XOR_CHECK_EQ(M.transform(float3( 0, 0, 1)), { -1, 0, -1});
    }
}

int main(int argc, char **argv)
{
    testTransformMatrices();
    return 0;
}
