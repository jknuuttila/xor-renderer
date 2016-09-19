#include "Xor/ProcessingMesh.hpp"
#include "Xor/Mesh.hpp"

namespace xor
{
    Mesh ProcessingMesh::mesh(Device & device) const
    {
        return Mesh::generate(device,
            { { "POSITION", DXGI_FORMAT_R32G32B32_FLOAT, asBytes(positions) } },
            indices);
    }
}
