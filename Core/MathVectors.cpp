#include "MathVectors.hpp"

namespace xor
{
    namespace math
    {
        Matrix Matrix::lookToDirection(float3 dir, float3 up)
        {
            float3 back  = -dir;
            float3 right = cross(up, back);

            // Invert rotation matrix with transpose
            return Matrix {
                { right.x, up.x, back.x, 0 },
                { right.y, up.y, back.y, 0 },
                { right.z, up.z, back.z, 0 },
                {       0,    0,      0, 1 },
            }.transpose();
        }

        Matrix Matrix::lookAt(float3 pos, float3 target, float3 up)
        {
            return lookToDirection(normalize(target - pos, { 0, 0, -1 }), up)
                * translation(-pos);
        }
    }
}
