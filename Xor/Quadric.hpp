#pragma once

#include "Core/Core.hpp"

namespace xor
{
    struct SimpleMesh
    {
        std::vector<float3> vertices;
        std::vector<uint> indices;
    };

    SimpleMesh quadricMeshSimplification(const SimpleMesh &inputMesh,
                                         uint targetTriangleCount);
}
