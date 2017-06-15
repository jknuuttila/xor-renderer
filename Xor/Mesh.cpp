#include "Xor/Mesh.hpp"
#include "Xor/Material.hpp"
#include "Xor/Xor.hpp"

#include "Core/Compression.hpp"

#include "external/assimp/assimp/Importer.hpp"
#include "external/assimp/assimp/scene.h"
#include "external/assimp/assimp/postprocess.h"

#include <vector>
#include <unordered_map>

namespace xor
{
    struct Mesh::State
    {
        String name;
        Material material;
        info::InputLayoutInfo inputLayout;
        std::vector<MeshData<BufferVBV>> vertexBuffers;
        MeshData<BufferIBV> indexBuffer;
        uint numIndices = 0;
        uint numVertices = 0;
    };

    struct MeshFileHeader
    {
        static const uint VersionNumber = 1;
    };

    Mesh::LoadedMeshFile Mesh::loadFromImported(const Info &meshInfo)
    {
        LoadedMeshFile loaded;

        String meshFilePath = "meshes/" + meshInfo.stem() + ".xmesh";

        ChunkFile meshFile(meshFilePath);
        meshFile.read();
        auto &main = meshFile.mainChunk();
        main.reader().readStruct<MeshFileHeader>();

        if (auto mats = main.maybeChunk("materials"))
        {
            for (auto kv : mats->allChunks())
            {
                Material mat(kv.first);
                uint matIndex = kv.second->reader().read<uint>();

                if (auto albedo = kv.second->maybeChunk("albedo"))
                    mat.albedo().filename = albedo->reader().readString();

                loaded.materials[matIndex] = std::move(mat);
            }
        }

        if (auto meshChunk = main.maybeChunk("meshes"))
        {
            for (auto kv : meshChunk->allChunks())
            {
                loaded.meshes.emplace_back();
                auto &dst = loaded.meshes.back().m_state;
                dst = std::make_shared<Mesh::State>();

                auto il = info::InputLayoutInfoBuilder();
                uint streams = 0;

                auto reader = kv.second->reader();
                dst->name = reader.readString();
                int matIndex = reader.read<int>();
                if (matIndex >= 0)
                {
                    auto it = loaded.materials.find(static_cast<uint>(matIndex));
                    if (it != loaded.materials.end())
                        dst->material = it->second;
                }

                const char *VertexAttributes[] =
                {
                    "POSITION",
                    "NORMAL",
                    "TANGENT",
                    "BINORMAL",
                    "TEXCOORD",
                    "COLOR",
                };

                for (auto attr : VertexAttributes)
                {
                    if (auto attrChunk = kv.second->maybeChunk(attr))
                    {
                        auto r = attrChunk->reader();
                        auto format = r.read<Format>();
                        il.element(attr, 0, format, streams);
                        dst->vertexBuffers.emplace_back();
                        auto &d = dst->vertexBuffers.back();
                        d.format = format;

                        uint decompressedSize = r.readLength();
                        auto compressed = r.readBlob();
                        d.data = decompressZstd(decompressedSize, compressed);

                        ++streams;
                    }
                }

                if (auto idxChunk = kv.second->maybeChunk("indices"))
                {
                    auto r = idxChunk->reader();
                    auto format = r.read<Format>();
                    dst->indexBuffer.format = format;

                    uint decompressedSize = r.readLength();
                    auto compressed = r.readBlob();
                    dst->indexBuffer.data = decompressZstd(decompressedSize, compressed);

                    dst->numIndices = static_cast<uint>(dst->indexBuffer.data.sizeBytes() / format.size());
                }

                dst->inputLayout = il;
                if (!dst->vertexBuffers.empty())
                {
                    auto &vb = dst->vertexBuffers.front();
                    dst->numVertices = static_cast<uint>(vb.data.sizeBytes() / vb.format.size());
                }
            }
        }

        return loaded;
    }

    void Mesh::importMeshes(const Info &meshInfo, LoadedMeshFile &loaded)
    {
        String meshFilePath = "meshes/" + meshInfo.stem() + ".xmesh";

        std::unordered_map<String, uint> materialIndices;
        for (auto &m : loaded.materials)
            materialIndices[m.second.name()] = m.first;

        auto indexForMaterial = [&] (const Material &mat)
        {
            auto it = materialIndices.find(mat.name());
            if (it == materialIndices.end())
                return -1;
            else
                return static_cast<int>(it->second);
        };

        ChunkFile meshFile(meshFilePath);
        auto &main = meshFile.mainChunk();
        main.writer().writeStruct(MeshFileHeader {});

        if (!loaded.materials.empty())
        {
            auto &mats = main.setChunk("materials");

            for (auto &m : loaded.materials)
            {
                auto &chunk = mats.setChunk(m.second.name());
                chunk.writer().write(m.first);

                if (!m.second.albedo().filename.empty())
                    chunk.setChunk("albedo").writer().writeString(m.second.albedo().filename);
            }
        }

        if (!loaded.meshes.empty())
        {
            auto &meshChunk = main.setChunk("meshes");

            uint meshNumber = 0;
            for (auto &m : loaded.meshes)
            {
                auto &chunk = meshChunk.setChunk(String::format("#%u", meshNumber));
                auto layout = m.inputLayout();

                auto writer = chunk.writer();
                writer.writeString(m.name());
                writer.write<int>(indexForMaterial(m.material()));

                for (uint s = 0; s < m.numVertexAttributes(); ++s)
                {
                    auto writer = chunk.setChunk(layout[s].SemanticName).writer();
                    auto &attr = m.vertexAttribute(s);
                    writer.write(attr.format);

                    auto decompressedSize = static_cast<uint>(attr.data.sizeBytes());
                    auto compressed = compressZstd(attr.data);

                    writer.writeLength(decompressedSize);
                    writer.writeBlob(compressed);
                }

                if (!m.indices().data.empty())
                {
                    auto writer = chunk.setChunk("indices").writer();

                    auto &idx = m.indices();
                    writer.write(idx.format);

                    auto decompressedSize = static_cast<uint>(idx.data.sizeBytes());
                    auto compressed = compressZstd(idx.data);

                    writer.writeLength(decompressedSize);
                    writer.writeBlob(compressed);
                }

                ++meshNumber;
            }
        }

        meshFile.write();
    }

    Mesh::LoadedMeshFile Mesh::loadFromSource(const Info & meshInfo)
    {
        LoadedMeshFile loaded;

        Assimp::Importer importer;
        auto scene = importer.ReadFile(
            meshInfo.filename.cStr(),
            aiProcess_Triangulate           |
            aiProcess_FlipUVs               |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType           |
            (meshInfo.calculateTangentSpace ?
             aiProcess_CalcTangentSpace : 0)
        );

        loaded.meshes.reserve(scene->mNumMeshes);

        for (uint m = 0; m < scene->mNumMaterials; ++m)
        {
            auto src = scene->mMaterials[m];

            aiString name;
            if (src->Get(AI_MATKEY_NAME, name))
                continue;

            Material dst(name.C_Str());

            aiString path;
            if (src->Get(AI_MATKEY_TEXTURE_DIFFUSE(0), path) == aiReturn_SUCCESS)
                dst.albedo().filename = path.C_Str();

            loaded.materials[m] = std::move(dst);
        }

        for (uint m = 0; m < scene->mNumMeshes; ++m)
        {
            auto src = scene->mMeshes[m];

            loaded.meshes.emplace_back();
            auto &dst = loaded.meshes.back().m_state;
            dst = std::make_shared<Mesh::State>();

            auto il = info::InputLayoutInfoBuilder();
            uint streams = 0;

            auto makeVB = [&] () -> MeshData<BufferVBV> &
            {
                dst->vertexBuffers.emplace_back();
                return dst->vertexBuffers.back();
            };

            dst->name = src->mName.C_Str();

            XOR_CHECK(src->HasPositions(), "Mesh without vertex positions");
            {
                auto &vb = makeVB();
                vb.format = DXGI_FORMAT_R32G32B32_FLOAT;

                il.element("POSITION", 0, vb.format, streams);
                ++streams;

                vb.data = asBytes(makeConstSpan(src->mVertices, src->mNumVertices));
            }

            if (src->HasNormals())
            {
                auto &vb = makeVB();
                vb.format = DXGI_FORMAT_R32G32B32_FLOAT;

                il.element("NORMAL", 0, vb.format, streams);
                ++streams;

                vb.data = asBytes(makeConstSpan(src->mNormals, src->mNumVertices));
            }

            if (src->HasTangentsAndBitangents())
            {
                XOR_CHECK(src->HasNormals(), "Mesh with tangents but without normals");

                auto &t = makeVB();
                t.format = DXGI_FORMAT_R32G32B32_FLOAT;
                il.element("TANGENT", 0, t.format, streams);
                ++streams;
                t.data = asBytes(makeConstSpan(src->mTangents, src->mNumVertices));

                auto &b = makeVB();
                b.format = DXGI_FORMAT_R32G32B32_FLOAT;
                il.element("BINORMAL", 0, b.format, streams);
                ++streams;
                b.data = asBytes(makeConstSpan(src->mBitangents, src->mNumVertices));
            }

            if (src->HasVertexColors(0))
            {
                auto &vb = makeVB();
                vb.format = DXGI_FORMAT_R32G32B32A32_FLOAT;

                il.element("COLOR", 0, vb.format, streams);
                ++streams;

                vb.data = asBytes(makeConstSpan(src->mColors[0], src->mNumVertices));
            }

            if (src->HasTextureCoords(0))
            {
                auto &vb = makeVB();
                vb.format = DXGI_FORMAT_R32G32B32_FLOAT;

                il.element("TEXCOORD", 0, vb.format, streams);
                ++streams;

                vb.data = asBytes(makeConstSpan(src->mTextureCoords[0], src->mNumVertices));
            }

            if (src->HasFaces())
            {
                std::vector<uint> indices;
                indices.reserve(src->mNumFaces * 3);

                for (uint f = 0; f < src->mNumFaces; ++f)
                {
                    auto &face = src->mFaces[f];
                    XOR_CHECK(face.mNumIndices == 3, "Mesh with non-triangle faces");
                    indices.emplace_back(face.mIndices[0]);
                    indices.emplace_back(face.mIndices[1]);
                    indices.emplace_back(face.mIndices[2]);
                }

                dst->numIndices = static_cast<uint>(indices.size());
                dst->indexBuffer.data   = asBytes(indices);
                dst->indexBuffer.format = DXGI_FORMAT_R32_UINT;
            }

            auto mat = loaded.materials.find(src->mMaterialIndex);
            if (mat != loaded.materials.end())
                dst->material = mat->second;

            dst->inputLayout = il;
            dst->numVertices = src->mNumVertices;
        }

        return loaded;
    }

    std::vector<Mesh> Mesh::loadFromFile(Device &device, const Info &meshInfo)
    {
        Timer time;
        size_t loadedBytes = 0;

        LoadedMeshFile loaded;

        if (meshInfo.import)
        {
            for (;;)
            {
                try
                {
                    loaded = loadFromImported(meshInfo);
                    break;
                }
                catch (const Exception &) {}

                try
                {
                    loaded = loadFromSource(meshInfo);
                    importMeshes(meshInfo, loaded);
                }
                catch (const Exception &) {}

                break;
            }
        }
        else
        {
            loaded = loadFromSource(meshInfo);
        }

        if (meshInfo.loadMaterials)
        {
            for (auto &m : loaded.materials)
            {
                m.second.load(device, Material::Builder()
                              .basePath(meshInfo.basePath())
                              .import(meshInfo.import));
            }
        }

        for (auto &m : loaded.meshes)
        {
            for (auto &attr : m.m_state->vertexBuffers)
            {
                attr.view = device.createBufferVBV(
                    Buffer::Info::fromBytes(attr.data, attr.format.asStructure()));
                loadedBytes += attr.data.sizeBytes();
                attr.data.release();
            }

            {
                auto &idx = m.m_state->indexBuffer;
                idx.view = device.createBufferIBV(
                    Buffer::Info::fromBytes(idx.data, idx.format));
                loadedBytes += idx.data.sizeBytes();
                idx.data.release();
            }
        }

        log("Mesh", "Loaded \"%s\" and %u materials in %.2f ms (%.2f MB / s)\n",
            meshInfo.filename.cStr(),
            meshInfo.loadMaterials ? static_cast<uint>(loaded.materials.size()) : 0,
            time.milliseconds(),
            time.bandwidthMB(loadedBytes));

        std::vector<Mesh> meshes { std::move(loaded.meshes) };
        return meshes;
    }

    Mesh Mesh::generate(Device & device,
						Span<const VertexAttribute> vertexAttributes,
                        Span<const uint> indices)
    {
        Mesh m;
        m.m_state = std::make_shared<State>();
        auto &s = *m.m_state;

        info::InputLayoutInfoBuilder il;

        using std::get;
        for (auto &&attr : vertexAttributes)
        {
            Format format = attr.format;
            il.element(attr.semantic, attr.index, format, static_cast<uint>(s.vertexBuffers.size()));
            s.vertexBuffers.emplace_back();
            auto &vb = s.vertexBuffers.back();
            vb.data   = attr.data;
            vb.format = format;
            vb.view   = device.createBufferVBV(Buffer::Info::fromBytes(vb.data, vb.format));
        }

        s.indexBuffer.data.resize(indices.sizeBytes());
        memcpy(s.indexBuffer.data.data(), indices.data(), indices.sizeBytes());
        s.indexBuffer.format = DXGI_FORMAT_R32_UINT;
        s.indexBuffer.view   = device.createBufferIBV(Buffer::Info::fromBytes(s.indexBuffer.data, s.indexBuffer.format));

        s.inputLayout = il;
        s.numVertices = static_cast<uint>(s.vertexBuffers.front().view.buffer()->size);
        s.numIndices  = static_cast<uint>(indices.size());

        return m;
    }

    info::InputLayoutInfo Mesh::inputLayout() const
    {
        return m_state->inputLayout;
    }

    void Mesh::setForRendering(CommandList & cmd) const
    {
        for (uint i = 0; i < numVertexAttributes(); ++i)
            cmd.setVBV(m_state->vertexBuffers[i].view, i);

        cmd.setIBV(m_state->indexBuffer.view);

        cmd.setTopology();
    }

    uint Mesh::numIndices() const
    {
        return m_state->numIndices;
    }

    uint Mesh::numVertices() const
    {
        return m_state->numVertices;
    }

    uint Mesh::numVertexAttributes() const
    {
        return static_cast<uint>(m_state->vertexBuffers.size());
    }

    Material Mesh::material()
    {
        return m_state->material;
    }

    const String & Mesh::name() const
    {
        return m_state->name;
    }

    MeshData<BufferVBV>& Mesh::vertexAttribute(uint index)
    {
        return m_state->vertexBuffers[index];
    }

    MeshData<BufferIBV>& Mesh::indices()
    {
        return m_state->indexBuffer;
    }

    Mesh::Mesh(Device &device, const Info & info)
    {
        *this = loadFromFile(device, info)[0];
    }

    namespace info
    {
        String MeshInfo::basePath() const
        {
            return fs::path(filename.stdString())
                .remove_filename()
                .c_str();
        }

        String MeshInfo::stem() const
        {
            return fs::path(filename.stdString())
                .stem()
                .c_str();
        }
    }
}

