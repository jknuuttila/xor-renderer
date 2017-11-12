#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"
#include "Xor/Blit.hpp"
#include "Xor/Mesh.hpp"
#include "Xor/DirectedEdge.hpp"
#include "Xor/Quadric.hpp"

#include "RenderTerrain.sig.h"
#include "VisualizeTriangulation.sig.h"
#include "ComputeNormalMap.sig.h"
#include "RenderTerrainAO.sig.h"
#include "ResolveTerrainAO.sig.h"
#include "AccumulateTerrainAO.sig.h"
#include "TerrainShadowFiltering.sig.h"

#include <random>
#include <unordered_set>

// TODO: Helper visualizations (lines etc.)
// TODO: Tiled meshing
// TODO: Continuous LOD
// TODO: Superfluous vertex removal

using namespace xor;

static const float ArcSecond = 30.87f;

static const float NearPlane = 1.f;

struct ErrorMetrics
{
    double l2    = 0;
    double l1    = 0;
    double l_inf = 0;
};

struct Heightmap
{
    Device *device = nullptr;
    Image height;
    TextureSRV heightSRV;
    Image color;
    TextureSRV colorSRV;
    int2 size;
    float2 worldSize;
    float texelSize;
    float minHeight = 1e10;
    float maxHeight = -1e10;

    Heightmap() = default;
    Heightmap(Device &device,
              StringView file,
              float texelSize = ArcSecond / 3.f,
              float heightMultiplier = 1)
    {
        this->device = &device;
        height = Image(Image::Builder().filename(file));

        if (height.format() == DXGI_FORMAT_R16_UNORM)
        {
            ImageData sourceHeight = height.imageData();
            RWImageData scaledHeight(height.size(), DXGI_FORMAT_R32_FLOAT);

            float heightCoeff = heightMultiplier / static_cast<float>(std::numeric_limits<uint16_t>::max());

            for (uint y = 0; y < scaledHeight.size.y; ++y)
            {
                for (uint x = 0; x < scaledHeight.size.x; ++x)
                {
                    uint16_t intHeight = sourceHeight.pixel<uint16_t>(uint2(x, y));
                    float fHeight = static_cast<float>(intHeight) * heightCoeff;
                    scaledHeight.pixel<float>(uint2(x, y)) = fHeight;
                }
            }

            height = Image(scaledHeight);
        }

        XOR_ASSERT(height.format() == DXGI_FORMAT_R32_FLOAT, "Expected a float heightmap");

        heightSRV = device.createTextureSRV(Texture::Info(height));

        size  = int2(height.size());
        this->texelSize = texelSize;
        worldSize = texelSize * float2(size);

#if defined(_DEBUG)
        minHeight = 340.f;
        maxHeight = 2600.f;
#else
        Timer t;
        auto size = height.size();
        auto sr   = height.imageData();
        for (uint y = 0; y < size.y; ++y)
        {
            for (float f : sr.scanline<float>(y))
            {
                minHeight = std::min(f, minHeight);
                maxHeight = std::max(f, maxHeight);
            }
        }
        log("Heightmap", "Scanned heightmap bounds in %.2f ms\n", t.milliseconds());
#endif
    }

    void setColor(Image colorMap)
    {
        color    = std::move(colorMap);
        colorSRV = device->createTextureSRV(info::TextureInfo(color));
    }
};

enum class TriangulationMode
{
    UniformGrid,
    IncMaxError,
    TiledUniformGrid,
};

enum class VisualizationMode
{
    Disabled,
    WireframeHeight,
    OnlyHeight,
    WireframeError,
    OnlyError,
    CPUError,
};

XOR_DEFINE_CONFIG_ENUM(RenderingMode,
    Height,
    Lighting,
    AmbientOcclusion,
    ShadowTerm);

XOR_DEFINE_CONFIG_ENUM(FilterKind,
                       Temporal,
                       TemporalFeedback,
                       Gaussian,
                       Median);

struct FilterPass
{
    FilterKind kind = FilterKind::Gaussian;
    bool bilateral = false;
    int size = 1;
};

XOR_CONFIG_WINDOW(Settings, 500, 100)
{
    XOR_CONFIG_ENUM(RenderingMode, renderingMode, "Rendering mode", RenderingMode::Lighting);

    XOR_CONFIG_GROUP(LightingProperties)
    {
        XOR_CONFIG_SLIDER(float, sunAzimuth, "Sun azimuth", Angle::degrees(45.f).radians, 0, 2 * Pi);
        XOR_CONFIG_SLIDER(float, sunElevation, "Sun elevation", Angle::degrees(45.f).radians, 0, Pi / 2);
        XOR_CONFIG_SLIDER(float, sunIntensity, "Sun intensity", 1);
        XOR_CONFIG_SLIDER(float3, ambient, "Ambient intensity", float3(.025f));

        float3 sunDirection() const
        {
            auto M = Matrix::azimuthElevation(Angle(sunAzimuth), Angle(sunElevation));
            return normalize(float3(M.transform(float3(0, 0, -1))));
        }
        float3 sunColor() const { return float3(sunIntensity); }
    } lighting;

    XOR_CONFIG_GROUP(ShadowFiltering)
    {
        XOR_CONFIG_SLIDER(float, shadowBias, "Shadow depth bias", 0.01f, 0, .03f);
        XOR_CONFIG_SLIDER(float, shadowSSBias, "Shadow slope scaled depth bias", -2, -10, 10);
        XOR_CONFIG_SLIDER(int, shadowDimExp, "Shadow map size exponent", 10, 8, 12);
        XOR_CONFIG_SLIDER(float, shadowNoiseAmplitude, "Shadow noise amplitude", 0, 0, 10);
        XOR_CONFIG_SLIDER(float, shadowHistoryBlend, "Shadow history blend", 0);
        XOR_CONFIG_SLIDER(int, shadowNoiseSamples, "Shadow noise samples", 0, 0, 8);
        XOR_CONFIG_SLIDER(int, noisePeriod, "Noise period", 8, 0, 8);
        XOR_CONFIG_SLIDER(int, frozenNoise, "Frozen noise", -1, -1, 7);
        XOR_CONFIG_CHECKBOX(shadowJitter, "Shadow jittering", false);
        XOR_CONFIG_CHECKBOX(pcfGaussian, "Gaussian PCF", false);

        std::vector<FilterPass> shadowFilters;

        virtual bool customUpdate() override
        {
            bool changed = false;
            ImGui::NewLine();
            changed |= updateFilterPasses();
            return changed;
        }

    private:
        bool updateFilterPasses()
        {
            bool changed = false;

            ImGui::Text("Shadow filter chain");
            ImGui::Indent();

            if (ImGui::Button("Add"))
                shadowFilters.emplace_back(FilterPass {});

            int itemToChange = -1;
            int newIndex  = 0;

            for (int i = 0; i < shadowFilters.size(); ++i)
            {
                ImGui::Separator();
                ImGui::PushID(i);

                int index = i;
                changed |= filterPass(shadowFilters[i], index);
                if (index != i && itemToChange < 0)
                {
                    itemToChange = i;
                    newIndex = index;
                }

                ImGui::PopID();
            }

            if (itemToChange >= 0)
            {
                if (newIndex < -1)
                {
                    shadowFilters.erase(shadowFilters.begin() + itemToChange);
                }
                else
                {
                    // Being moved
                    newIndex = std::min(newIndex, int(shadowFilters.size() - 1));
                    newIndex = std::max(newIndex, 0);

                    std::swap(shadowFilters[itemToChange], shadowFilters[newIndex]);
                }
            }

            ImGui::Separator();

            ImGui::Unindent();

            return true;
        }

        bool filterPass(FilterPass &p, int &index)
        {
            bool changed = false;

            ImGui::Indent();
            changed |= configEnumImguiCombo("Kind", p.kind);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Bilateral", &p.bilateral);
            ImGui::SameLine();
            changed |= ImGui::SliderInt("Size", &p.size, 1, 5);

            if (ImGui::Button("Up"))
            {
                --index;
                changed = true;
            }

            ImGui::SameLine();

            if (ImGui::Button("Down"))
            {
                ++index;
                changed = true;
            }

            ImGui::SameLine();

            if (ImGui::Button("Delete"))
            {
                index = -1000;
                changed = true;
            }

            ImGui::Unindent();
            return true;
        }
    } shadow;
} cfg_Settings;

struct BlueNoise
{
    static constexpr int Count = 16;

    Image blueNoise[Count];
    TextureSRV blueNoiseSRV[Count];

    BlueNoise() = default;
    BlueNoise(Device &device)
    {
        for (int i = 0; i < Count; ++i)
        {
            blueNoise[i] = Image(info::ImageInfo(
                String::format(XOR_DATA "/blue-noise/128_128/LDR_RGBA_%d.png", i)));
            blueNoiseSRV[i] = device.createTextureSRV(info::TextureInfo(blueNoise[i]));
        }
    }

    TextureSRV &srv(int frameNumber = 0)
    {
        return blueNoiseSRV[frameNumber % Count];
    }

    ImageData data(int frameNumber = 0)
    {
        return blueNoise[frameNumber % Count].imageData();
    }

    float4 sequentialNoise(int frameNumber = 0)
    {
        auto img     = data(0);
        uint2 coords = morton2DDecode(frameNumber);
        coords       = coords % img.size;
        auto noise   = img.pixel<ColorUnorm>(coords).toFloat4();
        return noise;
    }
};

struct TerrainTile
{
    float2 tileMin;
    float2 tileMax;
    Mesh mesh;
};

struct Terrain
{
    Device device;
    Heightmap *heightmap = nullptr;
    ImageData heightData;
    Rect area;
    TextureSRV cpuError;

    float2 worldMin;
    float2 worldMax;
    float worldHeight   = 0;
    float worldDiameter = 0;

    std::vector<TerrainTile> tiles;

    Terrain() = default;
    Terrain(Device device, Heightmap &heightmap)
    {
        this->device = device;
        this->heightmap = &heightmap;
        heightData = heightmap.height.imageData();

        uniformGrid(Rect::withSize(heightmap.size), 100);
    }

    template <typename DEMesh>
    Mesh gpuMesh(const DEMesh &mesh, float2 minUV = float2(0, 0), float2 maxUV = float2(1, 1))
    {
        auto verts    = mesh.vertices();
        auto numVerts = verts.size();
        std::vector<float2> normalizedPos(numVerts);
        std::vector<float>  height(numVerts);
        std::vector<float2> uv(numVerts);

        float2 dims = float2(heightmap->size);

        for (uint i = 0; i < numVerts; ++i)
        {
            auto &v          = verts[i];
            uv[i]            = float2(v.pos) / dims;
            normalizedPos[i] = remap(minUV, maxUV, float2(0), float2(1), uv[i]);
            height[i]        = heightData.pixel<float>(uint2(v.pos));
        }

        std::vector<uint> indices;
        auto deIndices = mesh.triangleIndices();
        indices.reserve(deIndices.size());
        XOR_ASSERT(deIndices.size() % 3 == 0, "Unexpected amount of indices");
        for (size_t i = 0; i < deIndices.size(); i += 3)
        {
            uint a = deIndices[i];
            uint b = deIndices[i + 1];
            uint c = deIndices[i + 2];

            // Negate CCW test because the positions are in UV coordinates,
            // which is left handed because +Y goes down
            bool ccw = !isTriangleCCW(normalizedPos[a], normalizedPos[b], normalizedPos[c]);

            if (ccw)
            {
                indices.emplace_back(a);
                indices.emplace_back(b);
                indices.emplace_back(c);
            }
            else
            {
                indices.emplace_back(a);
                indices.emplace_back(c);
                indices.emplace_back(b);
            }
        }

        VertexAttribute attrs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(normalizedPos) },
            { "POSITION", 1, DXGI_FORMAT_R32_FLOAT,    asBytes(height) },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(uv) },
        };

        return Mesh::generate(device, attrs, indices);
    }

    template <typename DEMesh>
    Mesh tipsifyMesh(const DEMesh &mesh, float2 minUV = float2(0, 0), float2 maxUV = float2(1, 1))
    {
        Timer timer;

        auto numVerts = mesh.numVertices();

        int seenVertexCounter = 0;
        std::vector<int> newVertexIndices;
        std::vector<int> vertexForNewIndex(numVerts);
        auto newVertexIdx = [&] (int v)
        {
            if (newVertexIndices[v] < 0)
            {
                int v_ = seenVertexCounter;
                ++seenVertexCounter;
                newVertexIndices[v] = v_;
                vertexForNewIndex[v_] = v;
                return v_;
            }
            else
            {
                return newVertexIndices[v];
            }
        };

        std::vector<int> recentVertices;
        std::vector<int> liveTriangles;
        std::vector<uint8_t> triangleEmitted;
        std::vector<uint> indices;

        constexpr int VertexCacheSize = 16;
        int vertexCacheTime = 0;
        std::vector<int> vertexCacheTimestamps;

        auto processVertex = [&] (int v)
        {
            int &age = vertexCacheTimestamps[v];
            if (vertexCacheTime - age >= VertexCacheSize)
            {
                // Not in cache
                age = vertexCacheTime;
                ++vertexCacheTime;
                recentVertices.emplace_back(v);
            }
        };

        {
            int arbitraryVertex = 0;

            int numVerts = mesh.numVertices();

            newVertexIndices.resize(numVerts, -1);
            liveTriangles.resize(numVerts);
            vertexCacheTimestamps.resize(numVerts, -2 * VertexCacheSize);
            triangleEmitted.resize(mesh.numTriangles());
            indices.reserve(mesh.numTriangles() * 3);

            for (int v = 0; v < numVerts; ++v)
            {
                mesh.vertexForEachTriangle(v, [&](int t)
                {
                    ++liveTriangles[v];
                });
            }

            int fanningVertex = -1;
            for (;;)
            {
                // If there is no valid vertex, pick the next vertex with some
                // triangles left.
                if (fanningVertex < 0)
                {
                    while (arbitraryVertex < numVerts)
                    {
                        if (liveTriangles[arbitraryVertex] > 0)
                        {
                            fanningVertex = arbitraryVertex;
                            break;
                        }

                        ++arbitraryVertex;
                    }

                    if (arbitraryVertex >= numVerts)
                        break;
                }

                XOR_ASSERT(fanningVertex >= 0, "No valid vertex");

                // Emit all triangles of the vertex
                mesh.vertexForEachTriangle(fanningVertex, [&] (int t)
                {
                    if (triangleEmitted[t])
                        return;

                    int3 vs = mesh.triangleVertices(t);

                    for (int v : vs.span())
                    {
                        XOR_ASSERT(liveTriangles[v] > 0, "Trying to reduce triangles from a fully processed vertex");
                        --liveTriangles[v];
                    }

                    processVertex(vs.x);
                    processVertex(vs.y);
                    processVertex(vs.z);

                    indices.emplace_back(newVertexIdx(vs.x));
                    indices.emplace_back(newVertexIdx(vs.y));
                    indices.emplace_back(newVertexIdx(vs.z));

                    triangleEmitted[t] = 1;
                });

                int oldestAge = -1;
                int nextVertex = -1;
                mesh.vertexForEachAdjacentVertex(fanningVertex, [&] (int v)
                {
                    int live = liveTriangles[v];
                    if (live == 0)
                        return;

                    int worstCaseVerts = live * 2;
                    int age = vertexCacheTime - vertexCacheTimestamps[v];

                    if (age + worstCaseVerts < VertexCacheSize)
                    {
                        // Vertex would still be in cache after emitting its triangles,
                        // and is thus valid.
                        if (oldestAge < age)
                        {
                            oldestAge = age;
                            nextVertex = v;
                        }
                    }
                });

                // If we don't have a valid vertex from the adjacent vertices,
                // try the recently processed vertices
                if (nextVertex < 0)
                {
                    while (!recentVertices.empty())
                    {
                        int v = recentVertices.back();
                        recentVertices.pop_back();

                        if (liveTriangles[v] > 0)
                        {
                            nextVertex = v;
                            break;
                        }
                    }
                }

                fanningVertex = nextVertex;
            }
        }

        std::vector<float2> normalizedPos(numVerts);
        std::vector<float>  height(numVerts);
        std::vector<float2> uv(numVerts);

        float2 dims = float2(heightmap->size);

        auto verts = mesh.vertices();

        for (int i = 0; i < numVerts; ++i)
        {
            auto &v          = verts[vertexForNewIndex[i]];
            uv[i]            = float2(v.pos) / dims;
            normalizedPos[i] = remap(minUV, maxUV, float2(0), float2(1), uv[i]);
            height[i]        = heightData.pixel<float>(uint2(v.pos));
        }

        XOR_ASSERT(indices.size() % 3 == 0, "Unexpected amount of indices");
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            uint a = indices[i];
            uint b = indices[i + 1];
            uint c = indices[i + 2];

            // Negate CCW test because the positions are in UV coordinates,
            // which is left handed because +Y goes down
            bool ccw = !isTriangleCCW(normalizedPos[a], normalizedPos[b], normalizedPos[c]);

            if (!ccw)
                std::swap(indices[i + 1], indices[i + 2]);
        }

        VertexAttribute attrs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(normalizedPos) },
            { "POSITION", 1, DXGI_FORMAT_R32_FLOAT,    asBytes(height) },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(uv) },
        };

        Mesh gpuMesh = Mesh::generate(device, attrs, indices);

        log("Heightmap", "Generated tipsified mesh with %d vertices and %d triangles in %.2f ms\n",
            mesh.numVertices(),
            mesh.numTriangles(),
            timer.milliseconds());

        return gpuMesh;
    }

    using Vert = Vector<int64_t, 3>;

    Vert vertex(int2 coords) const
    {
        float height = heightData.pixel<float>(uint2(coords));
        return Vert(coords.x, coords.y, int(height * float(0x1000)));
    }

    Vert vertex(float2 uv) const
    {
        return vertex(int2(heightData.unnormalized(uv)));
    }

    Vert vertex(Rect area, float2 uv)
    {
        float2 unnormalized = lerp(float2(area.min), float2(area.max), uv);
        return vertex(int2(unnormalized));
    }

    void singleTile(Rect area, Mesh m)
    {
        setBounds(area);
        tiles.resize(1);
        tiles.front().tileMin = worldMin;
        tiles.front().tileMax = worldMax;
        tiles.front().mesh    = std::move(m);
    }

    void uniformGrid(Rect area, uint quadsPerDim)
    {
        Timer t;

        area.max = min(area.max, heightmap->size);
        if (all(area.size() < int2(128)))
            area.min = area.max - 128;

        int2 sz        = int2(area.size());
        float2 szWorld = float2(sz) * heightmap->texelSize;

        int minDim = std::min(sz.x, sz.y);
        int vertexDistance = minDim / static_cast<int>(quadsPerDim);
        vertexDistance = std::max(1, vertexDistance);

        int2 verts = sz / vertexDistance;
        float2 fVerts = float2(verts);
        float2 fRes   = float2(sz);
        float2 worldMin = -szWorld / 2.f;
        float2 worldMax = worldMin + szWorld;

        auto numVerts   = (verts.x + 1) * (verts.y + 1);

        std::vector<float2> normalizedPos;
        std::vector<float>  heights;
        std::vector<float2> uvs;
        std::vector<uint>   indices;

        normalizedPos.reserve(numVerts);
        uvs.reserve(numVerts);
        heights.reserve(numVerts);
        indices.reserve(verts.x * verts.y * (3 * 2));

        float2 invSize = 1.f / float2(heightmap->size);

        for (int y = 0; y <= verts.y; ++y)
        {
            for (int x = 0; x <= verts.x; ++x)
            {
                int2 vertexGridCoords = int2(x, y);
                int2 texCoords = min(vertexGridCoords * vertexDistance + area.min, heightmap->size - 1);
                float2 uv = float2(vertexGridCoords * vertexDistance) / fRes;

                float height = heightData.pixel<float>(uint2(texCoords));

                normalizedPos.emplace_back(uv);
                heights.emplace_back(height);
                uvs.emplace_back((float2(texCoords) + 0.5f) * invSize);
            }
        }

        int vertsPerRow = verts.y + 1;
        for (int y = 0; y < verts.y; ++y)
        {
            for (int x = 0; x < verts.x; ++x)
            {
                uint ul = y * vertsPerRow + x;
                uint ur = ul + 1;
                uint dl = ul + vertsPerRow;
                uint dr = dl + 1;

                indices.emplace_back(ul);
                indices.emplace_back(dl);
                indices.emplace_back(ur);
                indices.emplace_back(dl);
                indices.emplace_back(dr);
                indices.emplace_back(ur);
            }
        }

        log("HeightmapRenderer", "Generated uniform grid mesh with %zu vertices and %zu indices in %.2f ms\n",
            normalizedPos.size(),
            indices.size(),
            t.milliseconds());

        VertexAttribute attrs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(normalizedPos) },
            { "POSITION", 1, DXGI_FORMAT_R32_FLOAT,    asBytes(heights) },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(uvs) },
        };

        singleTile(area, Mesh::generate(device, attrs, indices));
    }

    void incrementalMaxError(Rect area, uint vertices, bool tipsify = true)
    {
        Timer timer;

        struct TriangleError
        {
            int2 coords;
            float error = -1;
        };
        struct LargestError
        {
            int triangle = -1;
            float error  = std::numeric_limits<float>::max();

            LargestError(int tri = -1)
                : triangle(tri)
            {}
            LargestError(int tri, float error)
                : triangle(tri)
                , error(error)
            {}

            explicit operator bool() const { return error != std::numeric_limits<float>::max(); }

            bool operator<(const LargestError &e) const { return error < e.error; }
        };

        using DErr = DirectedEdge<TriangleError, Vert>;
        DErr mesh;

        Vert minBound = vertex(area, {0, 0});
        Vert maxBound = vertex(area, {1, 1});

        std::mt19937 gen(95832);

        std::priority_queue<LargestError> largestError;
        std::vector<int> newTriangles;

#if 0
        BowyerWatson<DErr> delaunay(mesh);
#else
        DelaunayFlip<DErr> delaunay(mesh);
#endif
        delaunay.superTriangle(minBound, maxBound);

        {
            int v0 = delaunay.insertVertex(vertex(area, { 1, 0 }));
            int v1 = delaunay.insertVertex(vertex(area, { 0, 1 }));
            int v2 = delaunay.insertVertex(vertex(area, { 0, 0 }));
            int v3 = delaunay.insertVertex(vertex(area, { 1, 1 }));

            for (int v : { v0, v1, v2, v3 })
            {
                mesh.vertexForEachTriangle(v, [&](int t)
                {
                    largestError.emplace(t);
                });
            }
        }

        XOR_ASSERT(!largestError.empty(), "No valid triangles to subdivide");

        std::unordered_set<int2, PodHash, PodEqual> usedVertices;

        // Subtract 3 from the vertex count to account for the supertriangle
        while (mesh.numVertices() - 3 < static_cast<int>(vertices))
        {
            auto largest = largestError.top();
            largestError.pop();
            int t = largest.triangle;

            if (t < 0 || !mesh.triangleIsValid(t) || delaunay.triangleContainsSuperVertices(t))
            {
                continue;
            }

            auto &triData = mesh.T(t);
            int3 verts  = mesh.triangleVertices(t);
            Vert v0     = mesh.V(verts.x).pos;
            Vert v1     = mesh.V(verts.y).pos;
            Vert v2     = mesh.V(verts.z).pos;

            // If the error isn't known, estimate it
            if (!largest.error || largest.error != triData.error)
            {
                int2 largestErrorCoords;
                float largestErrorFound = -1;

                // InteriorSamples == 1
                // [Heightmap]: L2: 4.532414e+05, L1: 6.876323e+08, L_inf: 7.701329e+02, Calculated for 152 triangles in 544.34 ms
                // [Heightmap]: L2: 1.928656e+05, L1: 2.663657e+08, L_inf: 7.379893e+02, Calculated for 1129 triangles in 556.19 ms
                // [Heightmap]: L2: 8.425315e+04, L1: 1.159352e+08, L_inf: 6.299168e+02, Calculated for 8413 triangles in 568.50 ms

                // InteriorSamples == 10
                // [Heightmap]: L2: 3.808483e+05, L1: 6.012014e+08, L_inf: 8.759630e+02, Calculated for 151 triangles in 544.98 ms
                // [Heightmap]: L2: 1.611922e+05, L1: 2.331848e+08, L_inf: 6.007446e+02, Calculated for 1123 triangles in 529.35 ms
                // [Heightmap]: L2: 6.603029e+04, L1: 1.004696e+08, L_inf: 3.462271e+02, Calculated for 8391 triangles in 560.90 ms

                // InteriorSamples == 100
                // [Heightmap]: L2: 4.335434e+05, L1: 6.780714e+08, L_inf: 7.855012e+02, Calculated for 151 triangles in 549.67 ms
                // [Heightmap]: L2: 1.692659e+05, L1: 2.508276e+08, L_inf: 3.902838e+02, Calculated for 1114 triangles in 557.31 ms
                // [Heightmap]: L2: 6.706991e+04, L1: 1.014288e+08, L_inf: 1.513256e+02, Calculated for 8383 triangles in 570.01 ms
                constexpr int InteriorSamples = 30;
                constexpr int EdgeSamples     = 0;

                auto errorAt = [&] (float3 bary)
                {
                    float3 interpolated = interpolateBarycentric(float3(v0), float3(v1), float3(v2), bary);
                    Vert point          = vertex(int2(interpolated));

                    float error = abs(float(point.z) - interpolated.z);
                    if (isPointInsideTriangleUnknownWinding(v0.vec2(), v1.vec2(), v2.vec2(), point.vec2())
                        && !usedVertices.count(int2(point))
                        && error > largestErrorFound)
                    {
                        largestErrorCoords = int2(point);
                        largestErrorFound  = error;
                    }
                };

                for (int i = 0; i < InteriorSamples; ++i)
                {
                    float3 bary = uniformBarycentricGen(gen);
                    errorAt(bary);
                }

                for (int i = 0; i < EdgeSamples; ++i)
                {
                    float x = std::uniform_real_distribution<float>()(gen);
                    int e = std::uniform_int_distribution<int>(0, 2)(gen);
                    float3 bary(0, x, 1 - x);
                    std::swap(bary[0], bary[e]);
                    errorAt(bary);
                }

                triData.coords = largestErrorCoords;
                triData.error  = largestErrorFound;

                largestError.emplace(t, largestErrorFound);
            }
            // The error is known, and it was the largest, so insert a new vertex
            // in that position.
            else
            {
                Vert newVertex = vertex(triData.coords);
                newTriangles.clear();
                delaunay.insertVertex(t, newVertex, &newTriangles);
                usedVertices.emplace(int2(newVertex));

                for (int nt : newTriangles)
                    largestError.emplace(nt);
            }
        }

        delaunay.removeSuperTriangle();

        mesh.vertexRemoveUnconnected();

        log("Heightmap", "Generated incremental max error triangulation with %d vertices and %d triangles in %.2f ms\n",
            mesh.numValidVertices(),
            mesh.numValidTriangles(),
            timer.milliseconds());

        if (tipsify)
        {
            singleTile(area,
                       tipsifyMesh(mesh,
                                   float2(area.min) / float2(heightmap->size),
                                   float2(area.max) / float2(heightmap->size)));
        }
        else
        {
            singleTile(area,
                       gpuMesh(mesh,
                               float2(area.min) / float2(heightmap->size),
                               float2(area.max) / float2(heightmap->size)));
        }
    }

    TerrainTile uniformGridTile(float2 posOffset, Rect area, uint quadsExp, bool tipsify = true)
    {
        uint2 areaSize     = uint2(area.size());
        uint sideLength    = std::max(areaSize.x, areaSize.y);

        XOR_ASSERT(roundUpToPow2(sideLength) == sideLength, "Side length must be a power of 2");

        uint quadsPerSide  = 2u << quadsExp;
        uint vertsPerSide  = quadsPerSide + 1;
        uint pixelsPerQuad = sideLength / quadsPerSide;

        TerrainTile tile;

        float2 minUV = float2(area.min) / float2(heightmap->size);
        float2 maxUV = float2(area.max) / float2(heightmap->size);
        tile.tileMin = worldCoords(area.min) + posOffset;
        tile.tileMax = worldCoords(area.max) + posOffset;

        float vertexDistance = float(pixelsPerQuad) * heightmap->texelSize;

        DirectedEdge<Empty, uint2> de;

        int numVertices = 0;
        uint2 maxCoords = uint2(heightmap->size - 1);

        for (uint y = 0; y < vertsPerSide; ++y)
        {
            for (uint x = 0; x < vertsPerSide; ++x)
            {
                uint2 pixelCoords = uint2(x, y) * uint2(pixelsPerQuad) + uint2(area.min);
                pixelCoords = min(pixelCoords, maxCoords);

                int v = de.addVertex(pixelCoords);
                XOR_ASSERT(v == numVertices, "Unexpected vertex number");

                ++numVertices;
            }
        }

        auto vertexNumber = [vertsPerSide](int x, int y)
        {
            return y * vertsPerSide + x;
        };

        // Loop all "even" vertices in the interior, generate triangles
        for (uint y = 1; y < vertsPerSide; y += 2)
        {
            for (uint x = 1; x < vertsPerSide; x += 2)
            {
                // numpad directions
                int v7 = vertexNumber(x - 1, y - 1);
                int v8 = vertexNumber(x + 0, y - 1);
                int v9 = vertexNumber(x + 1, y - 1);
                int v4 = vertexNumber(x - 1, y + 0);
                int v5 = vertexNumber(x + 0, y + 0);
                int v6 = vertexNumber(x + 1, y + 0);
                int v1 = vertexNumber(x - 1, y + 1);
                int v2 = vertexNumber(x + 0, y + 1);
                int v3 = vertexNumber(x + 1, y + 1);

                de.addTriangle(v5, v8, v7);
                de.addTriangle(v5, v9, v8);
                de.addTriangle(v5, v7, v4);
                de.addTriangle(v5, v6, v9);
                de.addTriangle(v5, v4, v1);
                de.addTriangle(v5, v3, v6);
                de.addTriangle(v5, v1, v2);
                de.addTriangle(v5, v2, v3);
            }
        }

        de.connectAdjacentTriangles();

        if (tipsify)
            tile.mesh = tipsifyMesh(de, minUV, maxUV);
        else
            tile.mesh = gpuMesh(de, minUV, maxUV);

        return tile;
    }

    void tiledUniformGrid(Rect area, uint tileSize, uint quadsExp, bool tipsify = true)
    {
        setBounds(area);

        tiles.clear();

        int2 midpoint = area.min + area.size() / int2(2);

        for (int y = area.min.y; y < area.max.y; y += int(tileSize))
        {
            for (int x = area.min.x; x < area.max.x; x += int(tileSize))
            {
                int2 coords = int2(x, y);
                tiles.emplace_back(uniformGridTile(
                    -worldCoords(midpoint),
                    Rect { coords, coords + int2(tileSize) },
                    quadsExp, tipsify));
            }
        }
    }

    ErrorMetrics calculateMeshError()
    {
        return ErrorMetrics {};
#if 0
        Timer timer;

        RWImageData error(uint2(area.size()), DXGI_FORMAT_R32_FLOAT);
        error.ownedData.fill(0);

        auto &uvAttr = mesh.vertexAttribute(2);
        XOR_ASSERT(uvAttr.format == DXGI_FORMAT_R32G32_FLOAT, "Unexpected format");

        auto uv = reinterpretSpan<const float2>(uvAttr.data);
        auto indices = reinterpretSpan<const uint>(mesh.indices().data);

        float2 dims = float2(heightmap->size);

        std::vector<uint8_t> once(heightmap->size.x * heightmap->size.y, 0);

        XOR_ASSERT(indices.size() % 3 == 0, "Unexpected amount of indices");
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            uint a = indices[i];
            uint b = indices[i + 1];
            uint c = indices[i + 2];

            float2 uv_a = uv[a];
            float2 uv_b = uv[b];
            float2 uv_c = uv[c];

            if (!isTriangleCCW(uv_a, uv_b, uv_c))
            {
                std::swap(b, c);
                std::swap(uv_b, uv_c);
            }

            int2 p_a = int2(uv_a * dims);
            int2 p_b = int2(uv_b * dims);
            int2 p_c = int2(uv_c * dims);

            float z_a = heightData.pixel<float>(uv_a);
            float z_b = heightData.pixel<float>(uv_b);
            float z_c = heightData.pixel<float>(uv_c);

            rasterizeTriangleCCWBarycentric(p_a, p_b, p_c, [&] (int2 p, float3 bary)
            {
                float z_p             = heightData.pixel<float>(p);
                float z_interpolated  = interpolateBarycentric(z_a, z_b, z_c, bary);
                double dz             = z_p - z_interpolated;
                error.pixel<float>(p - area.min) = float(dz);
            });
        }

        double rmsError = 0;
        double sumAbsError  = 0;
        double maxError     = 0;

        cpuError = device.createTextureSRV(info::TextureInfo(error));

        for (uint y = 0; y < error.size.y; ++y)
        {
            for (uint x = 0; x < error.size.x; ++x)
            {
                float e      = error.pixel<float>(uint2(x, y));
                double ae    = std::abs(e);
                rmsError    += e * e;
                sumAbsError += ae;
                maxError     = std::max(maxError, ae);
            }
        }

        rmsError = std::sqrt(rmsError);

        ErrorMetrics metrics;
        metrics.l2    = rmsError;
        metrics.l1    = sumAbsError;
        metrics.l_inf = maxError;

        log("Heightmap", "L2: %e, L1: %e, L_inf: %e, Calculated for %zu triangles in %.2f ms\n",
            metrics.l2,
            metrics.l1,
            metrics.l_inf,
            indices.size() / 3,
            timer.milliseconds());

        return metrics;
#endif
    }

    float2 worldCoords(int2 pixelCoords) const
    {
        pixelCoords -= heightmap->size / int2(2);
        return float2(pixelCoords) * heightmap->texelSize;
    }

    void setBounds(Rect area)
    {
        this->area = area;
        float2 texels = float2(area.size());
        float2 size   = texels * heightmap->texelSize;
        float2 extent = size / 2.f;
        worldMin = -extent;
        worldMax =  extent;

        float2 worldSize = worldMax - worldMin;
        worldHeight   = heightmap->maxHeight - heightmap->minHeight;
        worldDiameter = sqrt(worldSize.lengthSqr() + worldHeight * worldHeight);
    }

    void render(CommandList &cmd)
    {
        for (auto &t : tiles)
        {
            TerrainPatch::Constants constants;

            constants.tileMin   = t.tileMin;
            constants.tileMax   = t.tileMax;
            constants.heightMin = heightmap->minHeight;
            constants.heightMax = heightmap->maxHeight;

            cmd.setConstants(constants);

            t.mesh.setForRendering(cmd);

            cmd.drawIndexed(t.mesh.numIndices());
        }
    }

    info::InputLayoutInfo inputLayout()
    {
        return info::InputLayoutInfoBuilder()
            .element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0)
            .element("POSITION", 1, DXGI_FORMAT_R32_FLOAT,    1)
            .element("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2);
    }
};

struct TerrainRenderer
{
    using DE = DirectedEdge<Empty, int3>;

    Device device;
    GraphicsPipeline renderTerrain;
    GraphicsPipeline visualizeTriangulation;
    ComputePipeline computeNormalMapCS;
    ComputePipeline shadowFiltering;
    Terrain *terrain = nullptr;
    Heightmap *heightmap = nullptr;
    Rect area;
    float  maxErrorCoeff = .05f;
    VisualizationMode mode = VisualizationMode::WireframeHeight;
    RWTexture normalMap;
    RWTexture aoMap;
    RWTexture shadowMap;
    RWTexture shadowTerm[2];
    RWTexture shadowHistory;
    RWTexture motionVectors;
    BlueNoise blueNoise;
    Matrix prevViewProj = Matrix::identity();

    std::vector<info::ShaderDefine> lightingDefines;

    TerrainRenderer() = default;
    TerrainRenderer(Device device, Terrain &terrain, uint2 resolution)
    {
        this->device = device;
        this->terrain = &terrain;
        this->heightmap = terrain.heightmap;

        renderTerrain  = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("RenderTerrain.vs")
            .pixelShader("RenderTerrain.ps")
            .depthMode(info::DepthMode::Write)
            .depthFormat(DXGI_FORMAT_D32_FLOAT)
            .renderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            .inputLayout(terrain.inputLayout()));

        visualizeTriangulation  = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("VisualizeTriangulation.vs")
            .pixelShader("VisualizeTriangulation.ps")
            .renderTargetFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            .inputLayout(terrain.inputLayout()));

        computeNormalMapCS = device.createComputePipeline(
            ComputePipeline::Info("ComputeNormalMap.cs"));
        {
            normalMap = RWTexture(device, info::TextureInfoBuilder()
                                  .size(uint2(heightmap->size))
                                  .format(DXGI_FORMAT_R16G16B16A16_FLOAT)
                                  .allowUAV());

            auto cmd = device.graphicsCommandList();
            computeNormalMap(cmd);
            device.execute(cmd);
        }

        setShadowMapDim(1024);

        blueNoise = BlueNoise(device);

        {
            auto shadowTermInfo = info::TextureInfoBuilder()
                .size(resolution)
                .format(DXGI_FORMAT_R16_FLOAT)
                .allowRenderTarget()
                .allowUAV();
            shadowTerm[0]  = RWTexture(device, shadowTermInfo.debugName("shadowTerm0"));
            shadowTerm[1]  = RWTexture(device, shadowTermInfo.debugName("shadowTerm1"));
            shadowHistory = RWTexture(device, shadowTermInfo.debugName("shadowHistory"));
            auto cmd = device.graphicsCommandList();
            cmd.clearRTV(shadowTerm[0].rtv);
            cmd.clearRTV(shadowTerm[1].rtv);
            cmd.clearRTV(shadowHistory.rtv);
            device.execute(cmd);
        }

        shadowFiltering = device.createComputePipeline(
            info::ComputePipelineInfo("TerrainShadowFiltering.cs"));

        motionVectors = RWTexture(device, info::TextureInfoBuilder()
                               .size(resolution)
                               .format(DXGI_FORMAT_R16G16_FLOAT)
                               .allowRenderTarget());
    }

    void computeNormalMap(CommandList &cmd)
    {
        //auto e = cmd.profilingEventPrint("computeNormalMap");
        auto e = cmd.profilingEvent("computeNormalMap");

        cmd.bind(computeNormalMapCS);

        ComputeNormalMap::Constants constants;
        constants.size = normalMap.texture()->size;
        constants.axisMultiplier = float2(heightmap->texelSize);
        constants.heightMultiplier = 1.f;

        cmd.setConstants(constants);
        cmd.setShaderView(ComputeNormalMap::heightMap, heightmap->heightSRV);
        cmd.setShaderView(ComputeNormalMap::normalMap, normalMap.uav);

        cmd.dispatchThreads(ComputeNormalMap::threadGroupSize, uint3(constants.size));
    }

    void computeAmbientOcclusion(CommandList &cmd,
                                 SwapChain &sc,
                                 std::function<void()> wait,
                                 uint samples = 0,
                                 uint aoMapResolution = 2048,
                                 uint depthBufferResolution = 4096)
    {
        if (samples == 0)
        {
#if defined(_DEBUG)
            samples = 10;
#else
            samples = 1000;
#endif
        }
        // auto e = cmd.profilingEventPrint("computeAmbientOcclusion");
        auto e = cmd.profilingEvent("computeAmbientOcclusion");

        auto renderAO = device.createGraphicsPipeline(
            GraphicsPipeline::Info()
            .vertexShader("RenderTerrainAO.vs")
            .depthMode(info::DepthMode::Write)
            .depthFormat(DXGI_FORMAT_D32_FLOAT)
            .inputLayout(terrain->inputLayout()));
        auto accumulateAO = device.createComputePipeline(
            ComputePipeline::Info("AccumulateTerrainAO.cs"));
        auto resolveAO = device.createComputePipeline(
            ComputePipeline::Info("ResolveTerrainAO.cs"));

        auto renderAODepthPrepass         = renderAO.variant()
            .cull(D3D12_CULL_MODE_NONE);
        auto renderAOAccumulateVisibility = renderAO.variant()
            .pixelShader("RenderTerrainAO.ps")
            .depthMode(info::DepthMode::ReadOnly)
            .depthFunction(D3D12_COMPARISON_FUNC_EQUAL);

        aoMap = RWTexture(device, info::TextureInfoBuilder()
                          .size(uint2(aoMapResolution))
                          .format(DXGI_FORMAT_R16_FLOAT)
                          .allowUAV());
        RWTexture aoVisibilityBits = RWTexture(device, info::TextureInfoBuilder()
                                               .size(uint2(aoMapResolution))
                                               .format(DXGI_FORMAT_R32_UINT)
                                               .allowUAV());
        RWTexture aoVisibilitySamples = RWTexture(device, info::TextureInfoBuilder()
                                        .size(uint2(aoMapResolution))
                                        .format(DXGI_FORMAT_R32_UINT)
                                        .allowUAV());

        auto zBuffer = device.createTextureDSV(info::TextureInfoBuilder()
                                               .size(uint2(depthBufferResolution))
                                               .format(DXGI_FORMAT_D32_FLOAT));
#if 0
        auto zBufferSRV = device.createTextureSRV(zBuffer.texture());
        Blit blit(device);
#endif

        std::mt19937 gen(120495);

        float radius = terrain->worldDiameter / 2;

        cmd.clearUAV(aoVisibilitySamples.uav);
        cmd.clearUAV(aoVisibilityBits.uav);
        constexpr uint AOBitsPerPixel = 32;

        {
            uint i = 0;
            while (i < samples)
            {
                for (uint j = 0; j < AOBitsPerPixel; ++j)
                {
                    float worldDiameter = terrain->worldDiameter;

                    //float3 hemisphere = uniformHemisphereGen(gen);
                    float3 hemisphere = cosineWeightedHemisphereGen(gen);
                    float3 sampleCameraPos = hemisphere.s_xzy * radius;
                    Matrix view = Matrix::lookAt(sampleCameraPos, float3(0));
                    Matrix proj = Matrix::projectionOrtho(worldDiameter, worldDiameter, 1.f, worldDiameter);
                    Matrix viewProj = proj * view;

                    cmd.clearDSV(zBuffer);
                    cmd.setRenderTargets(zBuffer);
                    cmd.bind(renderAODepthPrepass);

                    RenderTerrainAO::Constants constants;
                    constants.viewProj = viewProj;
                    constants.worldMin = terrain->worldMin;
                    constants.worldMax = terrain->worldMax;
                    constants.aoTextureSize = float2(aoMap.texture()->size);
                    constants.aoBitMask = 1 << j;

                    cmd.setConstants(constants);
                    cmd.setShaderView(RenderTerrainAO::terrainAOVisibleBits, aoVisibilityBits.uav);

                    terrain->render(cmd);

                    cmd.bind(renderAOAccumulateVisibility);

                    terrain->render(cmd);
                }

                {
                    cmd.bind(accumulateAO);
                    AccumulateTerrainAO::Constants constants;
                    constants.size = uint2(aoMap.texture()->size);

                    cmd.setConstants(constants);
                    cmd.setShaderView(AccumulateTerrainAO::terrainAOVisibleSamples, aoVisibilitySamples.uav);
                    cmd.setShaderView(AccumulateTerrainAO::terrainAOVisibleBits, aoVisibilityBits.uav);

                    cmd.dispatchThreads(ResolveTerrainAO::threadGroupSize, uint3(constants.size));
                }

                i += AOBitsPerPixel;
            }
        }

        float maxVisibleSamples = float(samples);

        cmd.setRenderTargets();

        {
            constexpr int BlurTaps = 1;
            constexpr int NumWeights = BlurTaps + 1;

            cmd.bind(resolveAO);

            ResolveTerrainAO::Constants constants = {};
            constants.size              = int2(aoMap.texture()->size);
            constants.maxVisibleSamples = maxVisibleSamples;
            constants.blurKernelSize    = BlurTaps;

            for (int i = 0; i <= BlurTaps; ++i)
            {
                int n = BlurTaps * 2;
                int k = BlurTaps + i;
                int total = 1 << n;

                auto fact = [] (int x)
                {
                    int prod = 1;
                    for (int i = 2; i <= x; ++i)
                        prod *= i;
                    return prod;
                };

                int n_k = fact(n) / (fact(k) * fact(n - k));

                constants.blurWeights[i].x = float(n_k) / float(total);
            }

            cmd.setConstants(constants);
            cmd.setShaderView(ResolveTerrainAO::terrainAO, aoMap.uav);
            cmd.setShaderView(ResolveTerrainAO::terrainAOVisibleSamples, aoVisibilitySamples.srv);

            cmd.dispatchThreads(ResolveTerrainAO::threadGroupSize, uint3(constants.size));
        }
    }

    void updateLighting()
    {
        lightingDefines.clear();

        if (heightmap->colorSRV)
            lightingDefines.emplace_back("TEXTURED");

        auto renderingMode = cfg_Settings.renderingMode;

        if (renderingMode == RenderingMode::Lighting)
            lightingDefines.emplace_back("LIGHTING");
        else if (renderingMode == RenderingMode::AmbientOcclusion)
            lightingDefines.emplace_back("SHOW_AO");
        else if (renderingMode == RenderingMode::ShadowTerm)
            lightingDefines.emplace_back("SHADOW_TERM");

        setShadowMapDim(1 << cfg_Settings.shadow.shadowDimExp);
    }

    int noiseIndex()
    {
        if (cfg_Settings.shadow.noisePeriod <= 0)
            return int(device.frameNumber());
        else if (cfg_Settings.shadow.frozenNoise >= 0)
            return cfg_Settings.shadow.frozenNoise;
        else
            return int(device.frameNumber()) % cfg_Settings.shadow.noisePeriod;
    }

    void setShadowMapDim(int shadowDim)
    {
        if (!shadowMap.valid() || any(shadowMap.texture()->size != uint2(shadowDim)))
        {
            shadowMap = RWTexture(device, info::TextureInfoBuilder()
                                  .size(uint2(shadowDim))
                                  .format(DXGI_FORMAT_D32_FLOAT)
                                  .allowDepthStencil());
        }
    }

    void renderShadowMap(CommandList &cmd, const RenderTerrain::Constants &constants)
    {
        cmd.clearDSV(shadowMap.dsv);
        cmd.setRenderTargets(shadowMap.dsv);

        cmd.bind(renderTerrain.variant()
                 .pixelShader()
                 .renderTargetFormat()
                 .cull(D3D12_CULL_MODE_NONE)
                 .depthBias(0, cfg_Settings.shadow.shadowSSBias));

        auto c = constants;
        c.viewProj = c.shadowViewProj;
        cmd.setConstants(c);

        cmd.setShaderViewNullTextureSRV(RenderTerrain::terrainColor);
        cmd.setShaderViewNullTextureSRV(RenderTerrain::terrainNormal);
        cmd.setShaderViewNullTextureSRV(RenderTerrain::terrainAO);
        cmd.setShaderViewNullTextureSRV(RenderTerrain::terrainShadows);
        cmd.setShaderViewNullTextureSRV(RenderTerrain::noiseTexture);
        cmd.setShaderViewNullTextureSRV(RenderTerrain::shadowTerm);

        {
            auto p = cmd.profilingEvent("Draw shadows");
            terrain->render(cmd);
        }

        cmd.setRenderTargets();
    }

    RenderTerrain::Constants computeConstants(TextureRTV &rtv,
                                              const Matrix &viewProj)
    {
        RenderTerrain::Constants constants;

        float2 resolution = rtv.texture()->sizeFloat();

        float3 terrainMin = float3(terrain->worldMin.x, terrain->worldMin.y, heightmap->minHeight);
        float3 terrainMax = float3(terrain->worldMax.x, terrain->worldMax.y, heightmap->maxHeight);

        float3 terrainCorners[] =
        {
            float3(terrainMin.x, terrainMin.y, terrainMin.z),
            float3(terrainMin.x, terrainMin.y, terrainMax.z),
            float3(terrainMin.x, terrainMax.y, terrainMin.z),
            float3(terrainMin.x, terrainMax.y, terrainMax.z),
            float3(terrainMax.x, terrainMin.y, terrainMin.z),
            float3(terrainMax.x, terrainMin.y, terrainMax.z),
            float3(terrainMax.x, terrainMax.y, terrainMin.z),
            float3(terrainMax.x, terrainMax.y, terrainMax.z),
        };

        float3 terrainViewMin = float3(1e10f);
        float3 terrainViewMax = float3(-1e10f);

        float4 noise = blueNoise.sequentialNoise(noiseIndex());

        Angle shadowRotation = cfg_Settings.shadow.shadowJitter ? Angle::degrees(noise.z * 360) : Angle(0);
        float2 shadowJitter  = cfg_Settings.shadow.shadowJitter ? lerp(float2(-.5f), float2(.5f), noise.s_xy) : 0;

        Matrix R = Matrix::axisAngle(float3(0, 0, -1), shadowRotation);
        Matrix shadowView = R * Matrix::lookAt(cfg_Settings.lighting.sunDirection() * terrain->worldDiameter, float3(0));

        for (auto c : terrainCorners)
        {
            float3 c_ = float3(shadowView.transform(c));
            terrainViewMin = min(c_, terrainViewMin);
            terrainViewMax = max(c_, terrainViewMax);
        }

        float2 terrainDims = float2(max(abs(terrainViewMin), abs(terrainViewMax))) * 2;
        float terrainNear   = abs(terrainViewMin.z);
        float terrainFar    = abs(terrainViewMax.z);
        if (terrainNear > terrainFar)
            std::swap(terrainNear, terrainFar);

        terrainNear *= 0.9f;
        terrainFar  *= 1.1f;

        Matrix shadowProj = Matrix::projectionOrtho(float2(terrainDims),
                                                    terrainNear,
                                                    terrainFar);
        Matrix shadowViewProj = (shadowProj * shadowView)
            + Matrix::projectionJitter(shadowJitter * (2.f / shadowMap.texture()->sizeFloat()));

        constants.viewProj           = viewProj;
        constants.shadowViewProj     = shadowViewProj;
        constants.prevViewProj       = prevViewProj;
        constants.noiseResolution    = float2(blueNoise.srv().texture()->size);
        constants.noiseAmplitude     = cfg_Settings.shadow.shadowNoiseAmplitude / shadowMap.texture()->sizeFloat().x;
        constants.resolution         = rtv.texture()->sizeFloat();
        constants.shadowResolution   = shadowMap.texture()->sizeFloat();
        constants.shadowHistoryBlend = cfg_Settings.shadow.shadowHistoryBlend;
        constants.shadowBias         = cfg_Settings.shadow.shadowBias;
        constants.sunDirection       = cfg_Settings.lighting.sunDirection().s_xyz0;
        constants.sunColor           = cfg_Settings.lighting.sunColor().s_xyz0;
        constants.ambient            = float3(cfg_Settings.lighting.ambient).s_xyz0;

        return constants;
    }

    void render(CommandList &cmd,
                TextureRTV &rtv,
                TextureDSV &dsv,
                const Matrix &viewProj,
                bool wireframe = false)
    {
        updateLighting();

        RenderTerrain::Constants constants = computeConstants(rtv, viewProj);

        renderShadowMap(cmd, constants);

        {
            auto p = cmd.profilingEvent("Clear shadow targets");
            cmd.clearRTV(shadowTerm[0].rtv);
            cmd.clearRTV(shadowTerm[1].rtv);
            cmd.clearRTV(motionVectors.rtv);
        }

        {
            auto p = cmd.profilingEvent("Draw shadow prepass");

            cmd.bind(renderTerrain.variant()
                     .renderTargetFormats({ DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16G16_FLOAT })
                     .pixelShader(
                         "TerrainShadowPrepass.ps",
                         { info::ShaderDefine("TSP_NOISE_SAMPLES", cfg_Settings.shadow.shadowNoiseSamples) }));

            cmd.setRenderTargets({ &shadowTerm[0].rtv, &motionVectors.rtv }, dsv);

            cmd.setShaderView(RenderTerrain::terrainColor,   heightmap->colorSRV);
            cmd.setShaderView(RenderTerrain::terrainNormal,  normalMap.srv);
            cmd.setShaderView(RenderTerrain::terrainAO,      aoMap.srv);
            cmd.setShaderView(RenderTerrain::terrainShadows, shadowMap.srv);
            cmd.setShaderView(RenderTerrain::noiseTexture,   blueNoise.srv(noiseIndex()));
            cmd.setShaderViewNullTextureSRV(RenderTerrain::shadowTerm);

            cmd.setConstants(constants);

            terrain->render(cmd);
        }

        RWTexture *shadowIn  = &shadowTerm[0];
        RWTexture *shadowOut = &shadowTerm[1];

        {
            auto p = cmd.profilingEvent("Shadow filtering");

            for (auto &f : cfg_Settings.shadow.shadowFilters)
            {
                auto p = cmd.profilingEvent(xorConfigEnumValueName(f.kind),
                                            reinterpret_cast<uint64_t>(&f));

                if (f.kind == FilterKind::TemporalFeedback)
                {
                    cmd.copyTexture(shadowHistory.texture(), shadowIn->texture());
                }
                else
                {
                    const char *kindDefine = nullptr;
                    switch (f.kind)
                    {
                    case FilterKind::Temporal:
                        kindDefine = "TSF_FILTER_TEMPORAL";
                        break;
                    case FilterKind::Median:
                        kindDefine = "TSF_FILTER_MEDIAN";
                        break;
                    default:
                    case FilterKind::Gaussian:
                        kindDefine = "TSF_FILTER_GAUSSIAN";
                        break;
                    }

                    cmd.bind(shadowFiltering.variant()
                             .computeShader(info::SameShader{},
                             {
                                 { kindDefine },
                                 { "TSF_FILTER_WIDTH", f.size },
                                 { "TSF_BILATERAL",    f.bilateral },
                             }));

                    TerrainShadowFiltering::Constants tsfConstants;
                    tsfConstants.resolution = int2(rtv.texture()->size);
                    tsfConstants.shadowHistoryBlend = cfg_Settings.shadow.shadowHistoryBlend;

                    cmd.setShaderView(TerrainShadowFiltering::shadowOut,     shadowOut->uav);
                    cmd.setShaderView(TerrainShadowFiltering::shadowIn,      shadowIn->srv);
                    cmd.setShaderView(TerrainShadowFiltering::shadowHistory, shadowHistory.srv);
                    cmd.setShaderView(TerrainShadowFiltering::motionVectors, motionVectors.srv);
                    cmd.setConstants(tsfConstants);

                    cmd.dispatchThreads(TerrainShadowFiltering::threadGroupSize, uint3(tsfConstants.resolution));

                    std::swap(shadowIn, shadowOut);
                }
            }
        }

        {
            auto p = cmd.profilingEvent("Draw opaque");

            cmd.bind(renderTerrain.variant()
                     .depthFunction(D3D12_COMPARISON_FUNC_EQUAL)
                     .depthMode(info::DepthMode::ReadOnly)
                     .pixelShader(info::SameShader{}, lightingDefines));

            cmd.setRenderTargets(rtv, dsv);

            cmd.setShaderView(RenderTerrain::terrainColor,   heightmap->colorSRV);
            cmd.setShaderView(RenderTerrain::terrainNormal,  normalMap.srv);
            cmd.setShaderView(RenderTerrain::terrainAO,      aoMap.srv);
            cmd.setShaderView(RenderTerrain::terrainShadows, shadowMap.srv);
            cmd.setShaderView(RenderTerrain::noiseTexture,   blueNoise.srv(noiseIndex()));
            cmd.setShaderView(RenderTerrain::shadowTerm,     shadowIn->srv);

            cmd.setConstants(constants);

            terrain->render(cmd);
        }

        if (wireframe)
        {
            cmd.setRenderTargets(rtv, dsv);
            auto p = cmd.profilingEvent("Draw wireframe");
            cmd.bind(renderTerrain.variant()
                     .pixelShader(info::SameShader {}, { { "WIREFRAME" } })
                     .depthMode(info::DepthMode::ReadOnly)
                     .depthBias(10000)
                     .fill(D3D12_FILL_MODE_WIREFRAME));

            terrain->render(cmd);
        }

        cmd.setRenderTargets();
        prevViewProj = viewProj;
    }

    void visualize(CommandList &cmd, float2 minCorner, float2 maxCorner)
    {
        if (mode == VisualizationMode::Disabled)
            return;

        auto p = cmd.profilingEvent("Visualize triangulation");

        VisualizeTriangulation::Constants vtConstants;
        vtConstants.minHeight = heightmap->minHeight;
        vtConstants.maxHeight = heightmap->maxHeight;
        vtConstants.minCorner = minCorner;
        vtConstants.maxCorner = maxCorner;
        vtConstants.maxError  = maxErrorCoeff * (vtConstants.maxHeight - vtConstants.minHeight);

        if (mode == VisualizationMode::OnlyError || mode == VisualizationMode::WireframeError)
            cmd.bind(visualizeTriangulation.variant()
                     .pixelShader(info::SameShader {}, { { "SHOW_ERROR" } }));
        else if (mode == VisualizationMode::CPUError)
            cmd.bind(visualizeTriangulation.variant()
                     .pixelShader(info::SameShader {}, { { "CPU_ERROR" } }));
        else
            cmd.bind(visualizeTriangulation);

        cmd.setConstants(vtConstants);
        cmd.setShaderView(VisualizeTriangulation::heightMap,          heightmap->heightSRV);
        cmd.setShaderView(VisualizeTriangulation::cpuCalculatedError, terrain->cpuError);

        terrain->render(cmd);

        if (mode == VisualizationMode::WireframeHeight || mode == VisualizationMode::WireframeError)
        {
            cmd.bind(visualizeTriangulation.variant()
                     .pixelShader(info::SameShader{}, { { "WIREFRAME" } })
                     .fill(D3D12_FILL_MODE_WIREFRAME));
            terrain->render(cmd);
        }
    }
};

class TerrainRendering : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;
    TextureDSV depthBuffer;
    FPSCamera camera;
    Blit blit;

    Timer time;

    Heightmap heightmap;
    int2 areaStart = { 2000, 0 };
#if defined(_DEBUG)
    int areaSize  = 512;
#else
    int areaSize  = 2048;
#endif
    int triangulationDensity = 6;
    TriangulationMode triangulationMode = TriangulationMode::TiledUniformGrid;//TriangulationMode::UniformGrid;
    bool tipsifyMesh = true;
    bool blitArea    = true;
    bool blitNormal  = false;
    bool blitShadowMap = false;
    bool wireframe = false;
    bool largeVisualization = false;

    Terrain terrain;
    TerrainRenderer terrainRenderer;
public:
    TerrainRendering()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
#if 0
        , xor(Xor::DebugLayer::GPUBasedValidation)
#endif
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);
#if 1
        device      = xor.defaultDevice();
#else
        device      = xor.warpDevice();
#endif
        swapChain   = device.createSwapChain(*this);
        depthBuffer = device.createTextureDSV(Texture::Info(size(), DXGI_FORMAT_D32_FLOAT));
        blit        = Blit(device);

        Timer loadingTime;

#if defined(_DEBUG) || 1
        heightmap = Heightmap(device, XOR_DATA "/heightmaps/grand-canyon/floatn36w114_13.flt");
#else
        heightmap = Heightmap(device, XOR_DATA "/heightmaps/test/height.png",
                              0.5f, 440.f);
        heightmap.setColor(info::ImageInfo(XOR_DATA "/heightmaps/test/color.png"));
        areaStart = { 1024, 1024 };
        areaSize  = 4096;
#endif

        terrain = Terrain(device, heightmap);
        terrainRenderer = TerrainRenderer(device, terrain, swapChain.backbuffer().texture()->size);

        updateTerrain();

        camera.speed /= 10;
        camera.fastMultiplier *= 5;
    }

    void handleInput(const Input &input) override
    {
        auto imguiInput = device.imguiInput(input);
    }

    void keyDown(int keyCode) override
    {
        if (keyCode == VK_ESCAPE)
            terminate(0);
    }

    void updateTerrain()
    {
        auto area = Rect::withSize(areaStart, areaSize);

        switch (triangulationMode)
        {
        case TriangulationMode::UniformGrid:
        default:
            terrain.uniformGrid(area, quadsPerDim());
            break;
        case TriangulationMode::IncMaxError:
            terrain.incrementalMaxError(area, vertexCount(), tipsifyMesh);
            break;
        case TriangulationMode::TiledUniformGrid:
            terrain.tiledUniformGrid(area, 128, 2);
            break;
        }

        terrain.calculateMeshError();

        camera.position = float3(0, heightmap.maxHeight + NearPlane * 10, 0);

        {
            auto waitForKey = [&]() {
                while ((GetAsyncKeyState(VK_SPACE) & 0x8000)) { pumpMessages(); Sleep(1); }
                while (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) { pumpMessages(); Sleep(1); }
            };

            Timer aoTimer;
            auto cmd = device.graphicsCommandList();
            terrainRenderer.computeAmbientOcclusion(cmd, swapChain, waitForKey);
            device.execute(cmd);
            device.waitUntilCompleted(cmd.number());

            log("Heightmap", "Generated ambient occlusion map in %.2f ms\n",
                aoTimer.milliseconds());
        }
    }

    void measureTerrain()
    {
        auto area = Rect::withSize(areaStart, areaSize);

        constexpr int N = 18;
        std::vector<ErrorMetrics> uni(N);
        std::vector<ErrorMetrics> inc(N);

        for (int d = 2; d < N; ++d)
        {
            terrain.uniformGrid(area, quadsPerDim(d));
            uni[d] = terrain.calculateMeshError();

            terrain.incrementalMaxError(area, vertexCount(d), true);
            inc[d] = terrain.calculateMeshError();
        }

        print("%20s;%20s;%20s;%20s\n", "Vertices", "Uniform", "IncrementalMaxError", "Ratio");
        for (int d = 2; d < N; ++d)
        {
            print("%20d;%20e;%20e;%20f\n",
                  vertexCount(d),
                  uni[d].l2,
                  inc[d].l2,
                  uni[d].l2 / inc[d].l2);
        }

        updateTerrain();
    }

    int quadsPerDim(int density = 0) const
    {
        if (density == 0) density = triangulationDensity;
        return static_cast<int>(std::round(std::pow(std::sqrt(2), static_cast<float>(density))));
    }

    int vertexCount(int density = 0) const
    {
        int qpd = quadsPerDim(density);
        return (qpd + 1) * (qpd + 1);
    }

    void mainLoop(double deltaTime) override
    {
        camera.update(*this);

        auto cmd        = device.graphicsCommandList("Frame");
        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

        if (ImGui::Begin("Terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SliderInt("Size", &areaSize, 0, heightmap.size.x);
            if (ImGui::Button("Round size to power of two"))
                areaSize = roundUpToPow2(areaSize);

            ImGui::SliderInt2("Start", areaStart.data(), 0, heightmap.size.x - areaSize);
            ImGui::SliderInt("Density", &triangulationDensity, 5, 18);
            ImGui::Text("Vertex count: %d", vertexCount());

            ImGui::Separator();

            ImGui::Combo("Triangulation mode",
                         reinterpret_cast<int *>(&triangulationMode),
                         "Uniform grid\0"
                         "Incremental max error\0"
                         "Quadric\0");
            ImGui::Checkbox("Tipsify vertex cache optimization", &tipsifyMesh);

            ImGui::Separator();

            ImGui::Checkbox("Show area", &blitArea);
            ImGui::Checkbox("Show normals", &blitNormal);
            ImGui::Checkbox("Show shadows", &blitShadowMap);
            ImGui::Checkbox("Wireframe", &wireframe);

            ImGui::Separator();

            ImGui::Combo("Visualize triangulation",
                         reinterpret_cast<int *>(&terrainRenderer.mode),
                         "Disabled\0"
                         "WireframeHeight\0"
                         "OnlyHeight\0"
                         "WireframeError\0"
                         "OnlyError\0"
                         "CPUError\0");
            ImGui::Checkbox("Large visualization", &largeVisualization);
            ImGui::SliderFloat("Error magnitude", &terrainRenderer.maxErrorCoeff, 0, .25f);

            ImGui::Separator();

            if (ImGui::Button("Update"))
                updateTerrain();

            if (ImGui::Button("Measurement"))
                measureTerrain();
        }
        ImGui::End();

        {
            auto p = cmd.profilingEvent("Clear");
            cmd.clearRTV(backbuffer, float4(0, 0, 0, 1));
            cmd.clearDSV(depthBuffer, 0);
        }

        terrainRenderer.render(cmd,
                               backbuffer, depthBuffer,
                               Matrix::projectionPerspective(backbuffer.texture()->size, math::DefaultFov,
                                                             1.f, heightmap.worldSize.x * 1.5f)
                               * camera.viewMatrix(),
                               wireframe);

        cmd.setRenderTargets(backbuffer, depthBuffer);

        {
            float2 max = float2(1590, 890);
            float2 min;
            if (largeVisualization)
                min = max - 800;
            else
                min = max - 300;

            terrainRenderer.visualize(cmd,
                                      remap(float2(0), float2(backbuffer.texture()->size), float2(-1, 1), float2(1, -1), min),
                                      remap(float2(0), float2(backbuffer.texture()->size), float2(-1, 1), float2(1, -1), max));
        }

        cmd.setRenderTargets();

        if (blitArea && !blitNormal && !largeVisualization)
        {
            auto p = cmd.profilingEvent("Blit heightmap");
            float2 norm = normalizationMultiplyAdd(heightmap.minHeight, heightmap.maxHeight);

            blit.blit(cmd,
                      backbuffer, Rect::withSize(int2(backbuffer.texture()->size - 300).s_x0, 300),
                      heightmap.heightSRV, Rect::withSize(areaStart, areaSize),
                      norm.s_x000, norm.s_y001);
        }

        if (blitNormal && !largeVisualization)
        {
            auto p = cmd.profilingEvent("Blit normal map");

            blit.blit(cmd,
                      backbuffer, Rect::withSize(int2(backbuffer.texture()->size - 300).s_x0, 300),
                      terrainRenderer.normalMap.srv, Rect::withSize(areaStart, areaSize),
                      float4(0.5f, 0.5f, 1, 1), float4(0.5f, 0.5f, 0, 1));
        }

        if (blitShadowMap && !largeVisualization)
        {
            auto p = cmd.profilingEvent("Blit shadow map");

            blit.blit(cmd,
                      backbuffer, Rect(int2(200), int2(800)),
                      terrainRenderer.shadowMap.srv);
        }

#if 0
        blit.blit(cmd,
            backbuffer, Rect(int2(100), int2(800)),
            heightmapRenderer.aoMap.srv, Rect::withSize(areaStart, areaSize));
#endif

#if 0
        blit.blit(cmd,
            backbuffer, Rect(int2(100), int2(800)),
            heightmapRenderer.shadowHistory[0].srv);
#endif

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return TerrainRendering().run();
}
