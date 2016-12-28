#include "Xor/Quadric.hpp"

#include "external/quadric/src.cmd/Simplify.h"

namespace xor
{
    using namespace Simplify;

    SimpleMesh quadricMeshSimplification(const SimpleMesh &inputMesh, uint targetTriangleCount)
    {
        Timer timer;

        // The Simplify lib uses global variables declared in a header, so this is a bit dirty.
        vertices.clear();
        triangles.clear();

        vertices.reserve(inputMesh.vertices.size());
        triangles.reserve(inputMesh.indices.size() / 3);

        for (auto &v : inputMesh.vertices)
        {
            vertices.emplace_back();
            vertices.back().p = vec3f(v.x, v.y, v.z);
        }

        XOR_ASSERT(inputMesh.indices.size() % 3 == 0, "Expected triangle list size to be divisible by 3");

        for (uint i = 0; i < inputMesh.indices.size(); i += 3)
        {
            triangles.emplace_back();
            auto &t = triangles.back();
            t.v[0] = static_cast<int>(inputMesh.indices[i]);
            t.v[1] = static_cast<int>(inputMesh.indices[i + 1]);
            t.v[2] = static_cast<int>(inputMesh.indices[i + 2]);
        }

        simplify_mesh(static_cast<int>(targetTriangleCount));

        SimpleMesh outputMesh;
        outputMesh.vertices.reserve(vertices.size());
        outputMesh.indices.reserve(triangles.size() * 3);

        for (auto &v : vertices)
        {
            outputMesh.vertices.emplace_back(static_cast<float>(v.p.x),
                                             static_cast<float>(v.p.y),
                                             static_cast<float>(v.p.z));
        }

        for (auto &t : triangles)
        {
            outputMesh.indices.emplace_back(static_cast<uint>(t.v[0]));
            outputMesh.indices.emplace_back(static_cast<uint>(t.v[1]));
            outputMesh.indices.emplace_back(static_cast<uint>(t.v[2]));
        }

        vertices.clear();
        triangles.clear();

        log("Quadric", "Simplified mesh from %zu vertices and %zu triangles to %zu vertices and %zu triangles in %.3f ms\n",
            inputMesh.vertices.size(),
            inputMesh.indices.size() / 3,
            outputMesh.vertices.size(),
            outputMesh.indices.size() / 3,
            timer.milliseconds());

        return outputMesh;
    }
}
