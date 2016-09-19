#pragma once

#include "Core/Core.hpp"

namespace xor
{
    class Mesh;
    class Device;

    struct ProcessingMesh
    {
        std::vector<float3> positions;
        std::vector<uint>   indices;

        ProcessingMesh() = default;

        Mesh mesh(Device &device) const;
    };
}
