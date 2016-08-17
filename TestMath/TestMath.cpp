#include "Core/Core.hpp"

using namespace xor;
using namespace xor::math;

template <typename T> struct Epsilon { static double value() { return 0; } };
template <> struct Epsilon<float> { static double value() { return 0.001; } };
template <> struct Epsilon<double> { static double value() { return 0.001; } };
template <typename T, uint N> struct Epsilon<Vector<T, N>> { static double value() { return Epsilon<T>::value(); } };

bool all(bool b) { return b; }

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
void checkEqImpl(const char *file, int line, const T &a, const T &b, double epsilon = Epsilon<T>::value())
{
    if (!compareEq(a, b, epsilon))
    {
        auto msg = String::format("Values don't match: %s != %s",
                                  toString(a).cStr(),
                                  toString(b).cStr());
        print("%s(%d): %s\n", file, line, msg.cStr());
        XOR_CHECK(false, msg.cStr());
    }
}

#define XOR_CHECK_EQ(a, b, ...) checkEqImpl(__FILE__, __LINE__, a, b, ## __VA_ARGS__)

int main(int argc, char **argv)
{
    XOR_CHECK_EQ(1, 1);
    XOR_CHECK_EQ(float2(1, 1), float2(1, 1));
    XOR_CHECK_EQ(float2(1, 1), float2(1, 1.00001f));
    XOR_CHECK_EQ(float2(1, 1), float2(1, 1.002f));
    return 0;
}
