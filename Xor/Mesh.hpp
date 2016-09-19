#pragma once

#include "Core/Core.hpp"
#include "Xor/Xor.hpp"

namespace xor
{
    class Material;
    class Mesh;

    namespace info
    {
        class MeshInfo
        {
            friend class Mesh;
            String basePath() const;
            String stem() const;
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

    template <typename View = bool>
    struct MeshData
    {
        DynamicBuffer<uint8_t> data;
        Format format;
        View view = View {};

        MeshData() = default;
    };

    class Mesh
    {
    public:
        using Info    = info::MeshInfo;
        using Builder = info::MeshInfoBuilder;
    private:
        struct State;
        std::shared_ptr<State> m_state;

        struct LoadedMeshFile
        {
            std::vector<Mesh> meshes;
            std::unordered_map<uint, Material> materials;
        };
        static LoadedMeshFile loadFromImported(const Info & meshInfo);
        static LoadedMeshFile loadFromSource(const Info & meshInfo);
        static void importMeshes(const Info & meshInfo, LoadedMeshFile &loaded);

    public:

        Mesh() = default;
        Mesh(Device &device, const Info &meshInfo);

        static std::vector<Mesh> loadFromFile(Device &device, const Info &meshInfo);
        static Mesh generate(Device &device,
                             Span<const std::tuple<const char *, Format, Span<const uint8_t>>> vertexAttributes,
                             Span<const uint> indices = {});

        info::InputLayoutInfo inputLayout() const;
        void setForRendering(CommandList &cmd) const;
        uint numIndices() const;
        uint numVertices() const;
        uint numVertexAttributes() const;
        Material material();
        const String &name() const;

        MeshData<BufferVBV> &vertexAttribute(uint index);
        MeshData<BufferIBV> &indices();
    };
}
