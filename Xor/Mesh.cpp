#include "Xor/Mesh.hpp"
#include "Xor/Xor.hpp"

#include "external/assimp/assimp/Importer.hpp"
#include "external/assimp/assimp/scene.h"
#include "external/assimp/assimp/postprocess.h"

namespace xor
{
    struct Mesh::State
    {
        String name;
        info::InputLayoutInfo inputLayout;
        std::vector<BufferVBV> vertexBuffers;
        BufferIBV indexBuffer;
        uint numIndices = 0;
    };

    std::vector<Mesh> Mesh::loadFromFile(Device &device, const String & file)
    {
        Timer time;
        size_t loadedBytes = 0;

        Assimp::Importer importer;
        auto scene = importer.ReadFile(
            file.cStr(),
            aiProcess_CalcTangentSpace      |
            aiProcess_Triangulate           |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType);

        std::vector<Mesh> meshes;
        meshes.reserve(scene->mNumMeshes);

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

            dst->inputLayout = il;
        }

#if 0
        for (uint m = 0; m < scene->mNumMaterials; ++m)
        {
            auto mat = scene->mMaterials[m];
            log("loadMesh", "\nMATERIAL %u\n", m);

            for (uint p = 0; p < mat->mNumProperties; ++p)
            {
                auto prop = mat->mProperties[p];
                auto &key = prop->mKey;

                int i = -1;
                float f = -1;
                aiString s;

                switch (prop->mType)
                {
                case aiPTI_Integer:
                {
                    mat->Get(key.C_Str(), 0, 0, i);
                    log("loadMesh", "    %s: %d\n", key.C_Str(), i);
                    break;
                }
                case aiPTI_Float:
                {
                    //mat->Get(key.C_Str(), 0, 0, f);
                    log("loadMesh", "    %s: %f\n", key.C_Str(), f);
                    break;
                }
                case aiPTI_String:
                {
                    mat->Get(key.C_Str(), 0, 0, s);
                    log("loadMesh", "    %s: %s\n", key.C_Str(), s.C_Str());
                    break;
                }
                default:
                    break;
                }
            }
        }
#endif

        log("Mesh", "Loaded \"%s\" in %.2f ms (%.2f MB / s)\n",
            file.cStr(),
            time.milliseconds(),
            static_cast<double>(loadedBytes) / 1024 / 1024 / time.seconds());

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

    Mesh::Mesh(Device &device, const String & filename)
    {
        *this = loadFromFile(device, filename)[0];
    }
}
