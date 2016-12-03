#include "Core/Core.hpp"

using namespace xor;
using xor::math::Vector;
using xor::math::Matrix;
using xor::math::Angle;

template <typename T> struct Epsilon { static double value() { return 0; } };
template <> struct Epsilon<float> { static double value() { return 0.001; } };
template <> struct Epsilon<double> { static double value() { return 0.001; } };
template <typename T, uint N> struct Epsilon<Vector<T, N>> { static double value() { return Epsilon<T>::value(); } };
template <> struct Epsilon<Matrix> { static double value() { return Epsilon<float>::value(); } };

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

bool compareEq(const Matrix &a, const Matrix &b, double epsilon)
{
    return compareEq(a.row(0), b.row(0), epsilon)
        && compareEq(a.row(1), b.row(1), epsilon)
        && compareEq(a.row(2), b.row(2), epsilon)
        && compareEq(a.row(3), b.row(3), epsilon);
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

#define XOR_CHECK_EQ(...) XOR_DEBUG_BREAK_IF_FALSE(checkEqImpl(__FILE__, __LINE__, __VA_ARGS__))

void testBasicOperations()
{
    {
        Matrix A {
            { 1,  2,  3,  4},
            { 5,  6,  7,  8},
            { 9, 10, 11, 12},
            {13, 14, 15, 16},
        };
        Matrix B {
            {17, 18, 19, 20},
            {21, 22, 23, 24},
            {25, 26, 27, 28},
            {29, 30, 31, 32},
        };
        Matrix correct {
            {250, 260, 270, 280},
            {618, 644, 670, 696},
            {986, 1028, 1070, 1112},
            {1354, 1412, 1470, 1528}
        };

        XOR_CHECK_EQ(A * B, correct);
    }

    {
        Matrix A {
            { 1,  2,  3,  0},
            { 5,  6,  7,  8},
            { 9, 10,  0, 12},
            {13, 14, 15, 16},
        };

        Matrix correct = 1.f / 88.f * Matrix {
            {-44, -84, -8,  48 },
            { 66,  36, 16, -30 },
            {  0,   4, -8,   4 },
            {-22,  33,  0, -11 },
        };

        XOR_CHECK_EQ(A.inverse(), correct);
    }

    {
        float3x3 M {
            1.f, 2.f, -3.f,
            4.f, 5.f,  6.f,
            7.f, 8.f,  9.f
        };


        XOR_CHECK_EQ(M.determinant(), 18.f);
    }
}

void testTransformMatrices()
{
    {
        Matrix M  = Matrix::translation({1, 1, 1});
        float3 v  = { 1, 0, 0 };
        float3 v_ = M.transformAndProject(v);
        XOR_CHECK_EQ(v_, { 2, 1, 1 });
    }

    {
        Matrix M  = Matrix::lookInDirection({1, 0, 0}, {0, 1, 0});
        XOR_CHECK_EQ(M.transformAndProject(float3( 0,  0,  1)), { 1,  0,  0});
        XOR_CHECK_EQ(M.transformAndProject(float3( 1,  0,  0)), { 0,  0, -1});
        XOR_CHECK_EQ(M.transformAndProject(float3( 0,  1,  0)), { 0,  1,  0});
        XOR_CHECK_EQ(M.transformAndProject(float3( 0,  0, -1)), {-1,  0,  0});
        XOR_CHECK_EQ(M.transformAndProject(float3(-1,  0,  0)), { 0,  0,  1});
        XOR_CHECK_EQ(M.transformAndProject(float3( 0, -1,  0)), { 0, -1,  0});
    }

    {
        Matrix M  = Matrix::lookAt({1, 0, 0}, {0, 0, 0}, {0, 1, 0});
        XOR_CHECK_EQ(M.transformAndProject(float3( 0, 0, 1)), { -1, 0, -1});
    }
}

void testProjectionMatrices()
{
    {
        auto fov = Angle::degrees(90.f);
        float S  = tan(fov.radians / 2);
        Matrix M = Matrix::projectionPerspective(1.f, fov, 1, 2);
        XOR_CHECK_EQ(M.transform(float4( 0, 0, -1, 1)), { 0, 0, 1, 1});
        XOR_CHECK_EQ(M.transform(float4( 0, 0, -2, 1)), { 0, 0, 0, 2});
        XOR_CHECK_EQ(M.transform(float4( S, S, -1, 1)), { 1, 1, 1, 1});
        XOR_CHECK_EQ(M.transform(float4(S/2,S/2, -1, 1)), { 1./2, 1./2, 1, 1});
    }
}

void testGeometry()
{
    {
        float2 p1 { 1, 0 };
        float2 p2 { 0, 1 };
        float2 p3 { 0, 0 };
        auto circumcenter = circumcircleCenter(p1, p2, p3);
        float circumradius1 = (p1 - circumcenter).length();
        float circumradius2 = (p2 - circumcenter).length();
        float circumradius3 = (p3 - circumcenter).length();

        XOR_CHECK_EQ(circumradius1, circumradius2);
        XOR_CHECK_EQ(circumradius1, circumradius3);
    }
}

int main(int argc, char **argv)
{
    testBasicOperations();
    testTransformMatrices();
    testProjectionMatrices();
    testGeometry();
    return 0;
}
