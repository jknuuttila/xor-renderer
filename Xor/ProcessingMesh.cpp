#include "Xor/ProcessingMesh.hpp"
#include "Xor/Mesh.hpp"

namespace xor
{
    Mesh ProcessingMesh::mesh(Device & device) const
    {
		std::vector<VertexAttribute> attrs;
		attrs.reserve(2);

		if (!positions.empty()) attrs.emplace_back("POSITION", DXGI_FORMAT_R32G32B32_FLOAT, asBytes(positions));
		if (!uvs.empty())       attrs.emplace_back("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT, asBytes(uvs));

        return Mesh::generate(device, attrs, indices);
    }
}
