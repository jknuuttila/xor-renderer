#pragma once

#include "Core/Core.hpp"
#include "Xor/Xor.hpp"

namespace xor
{
    class Material;

    namespace info
    {
        class MeshInfo
        {
        public:
            String filename;
            bool calculateTangentSpace = true;
            bool loadMaterials = false;
            bool import = false;

            MeshInfo() = default;
            MeshInfo(String filename) : filename(std::move(filename)) {}
        };

        class MeshInfoBuilder : public MeshInfo
        {
        public:
            MeshInfoBuilder &filename(String path) { MeshInfo::filename = std::move(path); return *this; }
            MeshInfoBuilder &calculateTangentSpace(bool tangents = true) { MeshInfo::calculateTangentSpace = tangents; return *this; }
            MeshInfoBuilder &loadMaterials(bool load = true) { MeshInfo::loadMaterials = load; return *this; }
            MeshInfoBuilder &import(bool import = true) { MeshInfo::import = import; return *this; }
        };
    }

    class Mesh
    {
        struct State;
        std::shared_ptr<State> m_state;

    public:
        using Info    = info::MeshInfo;
        using Builder = info::MeshInfoBuilder;

        Mesh() = default;
        Mesh(Device &device, const Info &meshInfo);

        static std::vector<Mesh> loadFromFile(Device &device, const Info &meshInfo);

        info::InputLayoutInfo inputLayout() const;
        void setForRendering(CommandList &cmd) const;
        uint numIndices() const;
        Material material();
    };
}
