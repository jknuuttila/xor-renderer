#include "Xor/Mesh.hpp"
#include "Xor/Material.hpp"
#include "Xor/Xor.hpp"

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
        std::vector<BufferVBV> vertexBuffers;
        BufferIBV indexBuffer;
        uint numIndices = 0;
    };

    struct MeshFileHeader
    {
        static const uint VersionNumber = 1;

        uint numMeshes = 0;
        uint numMaterials = 0;
    };

    std::vector<Mesh> Mesh::loadFromFile(Device &device, const Info &meshInfo)
    {
        Timer time;
        size_t loadedBytes = 0;

        String fileStem = fs::path(meshInfo.filename.stdString())
            .stem()
            .c_str();
        String basePath = fs::path(meshInfo.filename.stdString())
            .remove_filename()
            .c_str();


        Timer importTime;

        std::vector<Mesh> meshes;
        std::unordered_map<uint, Material> materials;

        bool loadedImported = false;

        if (false && meshInfo.import)
        {
#if 0
            String modelFilePath = "models/" + fileStem + ".xmesh";
            ChunkFile modelFile(modelFilePath);

            for (;;)
            {
                try
                {
                    modelFile.read();
                    auto reader = modelFile.toc();

                    if (auto mats = reader.maybeKeyValue("materials"))
                    {
                        for (auto kv : mats->allKeyValues())
                        {
                            Material mat(mats.chunk("header").reader().readString());

                            auto kv = mats.readKeyValue();

                            if (auto albedo = kv->maybeChunk("albedo"))
                                mat.albedo().filename = albedo->reader().readString();

                            mat.load(device, Material::Builder()
                                     .basePath(basePath)
                                     .import(meshInfo.import));
                            materials[i] = std::move(mat);
                        }
                    }

                    if (header.numMeshes > 0)
                    {
                        auto meshChunk = reader.chunk("meshes").reader();

                        for (uint i = 0; i < header.numMaterials; ++i)
                        {
                            meshes.emplace_back();
                            auto &dst = meshes.back().m_state;
                            dst = std::make_shared<Mesh::State>();

                            auto il = info::InputLayoutInfoBuilder();
                            uint streams = 0;

                            dst->name = meshChunk.readString();

                            int material = meshChunk.read<int>();
                            if (material >= 0)
                            {
                                auto it = materials.find(static_cast<uint>(material));
                                if (it != materials.end())
                                    dst->material = it->second;
                            }

                            auto kv = meshChunk.readKeyValue();

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
                                if (auto attrChunk = kv->maybeChunk(attr))
                                {
                                    auto r = attrChunk->reader();
                                    auto format = r.read<Format>();
                                    il.element(attr, 0, format, streams);
                                    dst->vertexBuffers.emplace_back(device.createBufferVBV(
                                        Buffer::Info(r.readBlob())));
                                    loadedBytes += dst->vertexBuffers.back().buffer()->sizeBytes();
                                    ++streams;
                                }
                            }

                            if (auto idxChunk = kv->maybeChunk("indices"))
                            {
                                auto r = idxChunk->reader();
                                auto format = r.read<Format>();
                                auto indices = r.readBlob();
                                dst->indexBuffer = device.createBufferIBV(
                                    Buffer::Info(indices, format));
                                loadedBytes += dst->indexBuffer.buffer()->sizeBytes();
                                dst->numIndices = static_cast<uint>(indices.sizeBytes() / format.size());
                            }
                        }
                    }

                    loadedImported = true;
                    break;
                }
                catch (const Exception &) {}

                try
                {
                }
                catch (const Exception &) {}
            }
#endif
        }
        else
        {
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
            log("Mesh", "Loaded and processed \"%s\" in %.2f ms\n",
                meshInfo.filename.cStr(),
                importTime.milliseconds());

            meshes.reserve(scene->mNumMeshes);

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

                if (meshInfo.loadMaterials)
                    dst.load(device,
                             Material::Builder()
                             .basePath(basePath)
                             .import(meshInfo.import));

                materials[m] = std::move(dst);
            }

            for (uint m = 0; m < scene->mNumMeshes; ++m)
            {
                auto src = scene->mMeshes[m];

                meshes.emplace_back();
                auto &dst   = meshes.back().m_state;
                dst = std::make_shared<Mesh::State>();

                auto il = info::InputLayoutInfoBuilder();
                uint streams = 0;

                dst->name = src->mName.C_Str();

                XOR_CHECK(src->HasPositions(), "Mesh without vertex positions");
                {
                    il.element("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, streams);
                    ++streams;

                    dst->vertexBuffers.emplace_back(device.createBufferVBV(
                        Buffer::Info(makeConstSpan(src->mVertices, src->mNumVertices))));
                    loadedBytes += dst->vertexBuffers.back().buffer()->sizeBytes();
                }

                if (src->HasNormals())
                {
                    il.element("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, streams);
                    ++streams;

                    dst->vertexBuffers.emplace_back(device.createBufferVBV(
                        Buffer::Info(makeConstSpan(src->mNormals, src->mNumVertices))));
                    loadedBytes += dst->vertexBuffers.back().buffer()->sizeBytes();
                }

                if (src->HasTangentsAndBitangents())
                {
                    XOR_CHECK(src->HasNormals(), "Mesh with tangents but without normals");
                    il.element("TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, streams);
                    ++streams;
                    il.element("BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, streams);
                    ++streams;

                    dst->vertexBuffers.emplace_back(device.createBufferVBV(
                        Buffer::Info(makeConstSpan(src->mTangents, src->mNumVertices))));
                    loadedBytes += dst->vertexBuffers.back().buffer()->sizeBytes();

                    dst->vertexBuffers.emplace_back(device.createBufferVBV(
                        Buffer::Info(makeConstSpan(src->mBitangents, src->mNumVertices))));
                    loadedBytes += dst->vertexBuffers.back().buffer()->sizeBytes();
                }

                if (src->HasVertexColors(0))
                {
                    il.element("COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, streams);
                    ++streams;

                    dst->vertexBuffers.emplace_back(device.createBufferVBV(
                        Buffer::Info(makeConstSpan(src->mColors[0], src->mNumVertices))));
                    loadedBytes += dst->vertexBuffers.back().buffer()->sizeBytes();
                }

                if (src->HasTextureCoords(0))
                {
                    il.element("TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, streams);
                    ++streams;

                    dst->vertexBuffers.emplace_back(device.createBufferVBV(
                        Buffer::Info(makeConstSpan(src->mTextureCoords[0], src->mNumVertices))));
                    loadedBytes += dst->vertexBuffers.back().buffer()->sizeBytes();
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

                    dst->indexBuffer = device.createBufferIBV(
                        Buffer::Info(asConstSpan(indices), DXGI_FORMAT_R32_UINT));
                    loadedBytes += dst->indexBuffer.buffer()->sizeBytes();
                    dst->numIndices = static_cast<uint>(indices.size());
                }

                auto mat = materials.find(src->mMaterialIndex);
                if (mat != materials.end())
                    dst->material = mat->second;

                dst->inputLayout = il;
            }
        }

        log("Mesh", "Loaded \"%s\" and %u materials in %.2f ms (%.2f MB / s)\n",
            meshInfo.filename.cStr(),
            meshInfo.loadMaterials ? static_cast<uint>(materials.size()) : 0,
            time.milliseconds(),
            time.bandwidthMB(loadedBytes));

        return meshes;
    }

    info::InputLayoutInfo Mesh::inputLayout() const
    {
        return m_state->inputLayout;
    }

    void Mesh::setForRendering(CommandList & cmd) const
    {
        cmd.setVBVs(m_state->vertexBuffers);
        cmd.setIBV(m_state->indexBuffer);
        cmd.setTopology();
    }

    uint Mesh::numIndices() const
    {
        return m_state->numIndices;
    }

    Material Mesh::material()
    {
        return m_state->material;
    }

    Mesh::Mesh(Device &device, const Info & info)
    {
        *this = loadFromFile(device, info)[0];
    }
}
