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

// Top CPU problems:
// - CopyDescriptors
// - Root argument setting
// - CreateConstantBufferView
// - CPUVisibleHeap::flushBlock

// TODO: Superfluous vertex removal

using namespace Xor;

static const float ArcSecond = 30.87f;

static const float NearPlane = 1.f;

static constexpr float LinearLODCutoff = 1.05f;

struct ErrorMetrics
{
    double l2    = 0;
    double l1    = 0;
    double l_inf = 0;
};

enum class TriangulationMode
{
    UniformGrid,
    IncMaxError,
};

XOR_DEFINE_CONFIG_ENUM(VisualizationMode,
    Disabled,
    WireframeHeight,
    OnlyHeight,
    WireframeError,
    OnlyError,
    CPUError,
    LODLevel,
    ContinuousLOD,
    ClusterId);

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

XOR_DEFINE_CONFIG_ENUM(LodMode,
                       NoSnap,
                       Snap,
                       Morph);

struct FilterPass
{
    FilterKind kind = FilterKind::Gaussian;
    bool bilateral = false;
    int size = 1;
};

#ifdef _DEBUG
constexpr int LODCountDefault           = 3;
constexpr int LODVertexCountBaseDefault = 5 * 5;
#else
constexpr int LODCountDefault           = 5;
constexpr int LODVertexCountBaseDefault = 9 * 9;
#endif

XOR_CONFIG_WINDOW(Settings, 500, 100)
{
    XOR_CONFIG_ENUM(RenderingMode, renderingMode, "Rendering mode", RenderingMode::Lighting);
    XOR_CONFIG_SLIDER(int, lodCount, "LOD count", LODCountDefault, 1, 10);
    XOR_CONFIG_SLIDER(int, lodVertexBase, "LOD vertex count base", LODVertexCountBaseDefault, 4, 8192);
    XOR_CONFIG_SLIDER(float, lodVertexExponent, "LOD vertex count exponent", 4.f, 2.f, 8.f);
    XOR_CONFIG_SLIDER(float, lodSwitchDistance, "LOD switch distance", 1500.f, 100.f, 5000.f);
    XOR_CONFIG_SLIDER(float, lodSwitchExponent, "LOD switch exponent", 1.f, 1.f, 4.f);
    XOR_CONFIG_SLIDER(float, lodBias, "LOD bias", 0.f, -5.f, 5.f);
    XOR_CONFIG_ENUM(LodMode, lodMode, "LOD mode", LodMode::NoSnap);
    XOR_CONFIG_SLIDER(int, renderLod, "Rendered LOD", -1, -1, 10);
    XOR_CONFIG_CHECKBOX(vertexCulling, "Vertex LOD culling", true);
    XOR_CONFIG_CHECKBOX(highlightCracks, "Highlight cracks", false);
    XOR_CONFIG_CHECKBOX(vsync, "Vsync", true);

    int lodVertexCount(int lod) const
    {
        double vertexCountMultiplier = std::pow(double(lodVertexExponent), double(lod));
        int numLodVertices = int(std::round(lodVertexBase * vertexCountMultiplier));
        return numLodVertices;
    }

    bool isLinearLOD() const { return lodSwitchExponent < LinearLODCutoff; }

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

struct TerrainCluster
{
    Block32 indices;
    Rect aabb;
};

struct TerrainLOD
{
    Mesh mesh;
    std::vector<TerrainCluster> clusters;
};

struct Terrain
{
    Device device;
    Heightmap *heightmap = nullptr;
    ImageData heightData;
    Rect area;
    TextureSRV cpuError;

    int2 worldMin;
    int2 worldMax;
    int2 worldCenter;
    float worldHeight   = 0;
    float worldDiameter = 0;

    std::vector<TerrainLOD> terrainLods;

    Terrain() = default;
    Terrain(Device device, Heightmap &heightmap)
    {
        this->device = device;
        this->heightmap = &heightmap;
        heightData = heightmap.height.imageData();

        uniformGrid(Rect::withSize(heightmap.size));
    }

    struct VertexLod
    {
        int2 nextLodPixelCoords;
    };

    static int2 vertexNextLodPixelCoords(const Empty &, int2 pixelCoords)
    {
        return pixelCoords;
    }

    static int2 vertexNextLodPixelCoords(const VertexLod &v, int2 pixelCoords)
    {
        return v.nextLodPixelCoords;
    }

    template <typename DEMesh>
    Mesh gpuMesh(const DEMesh &mesh,
                 std::vector<int> indices = {},
                 Span<const int> vertexForIndex = {})
    {
        return gpuMeshFromBuffers(mesh.exportMesh(),
                                  std::move(indices),
                                  vertexForIndex);
    }

    template <typename MeshBuffers>
    Mesh gpuMeshFromBuffers(const MeshBuffers &meshBuffers,
                            std::vector<int> indices = {},
                            Span<const int> vertexForIndex = {})
    {
        auto &vb = meshBuffers.vb;

        auto ib = std::move(indices);
        if (ib.empty())
            ib = meshBuffers.ib; 

        int numVerts = int(vb.size());

        std::vector<int2>  pixelCoords(numVerts);
        std::vector<float> height(numVerts);
        std::vector<int2>  nextLodPixelCoords(numVerts);
        std::vector<float> nextLodHeight(numVerts);
        std::vector<float> longestEdge(numVerts, 0);

        float2 dims = float2(heightmap->size);

        for (int i = 0; i < numVerts; ++i)
        {
            int vertexIndex       = vertexForIndex.empty() ? i : vertexForIndex[i];
            auto &v               = vb[vertexIndex];
            pixelCoords[i]        = int2(v.pos);
            height[i]             = heightData.pixel<float>(uint2(v.pos));
            nextLodPixelCoords[i] = vertexNextLodPixelCoords(v, pixelCoords[i]);
            nextLodHeight[i]      = heightData.pixel<float>(uint2(nextLodPixelCoords[i]));
        }

        XOR_ASSERT(ib.size() % 3 == 0, "Unexpected amount of indices");
        for (size_t i = 0; i < ib.size(); i += 3)
        {
            uint a = ib[i];
            uint b = ib[i + 1];
            uint c = ib[i + 2];

            int2 pA = pixelCoords[a];
            int2 pB = pixelCoords[b];
            int2 pC = pixelCoords[c];

            // Negate CCW test because the positions are in UV coordinates,
            // which is left handed because +Y goes down
            bool ccw = !isTriangleCCW(pA, pB, pC);

            if (!ccw)
                std::swap(ib[i + 1], ib[i + 2]);

            float &longestA = longestEdge[a];
            float &longestB = longestEdge[b];
            float &longestC = longestEdge[c];
            float2 wA = worldCoords(pA);
            float2 wB = worldCoords(pB);
            float2 wC = worldCoords(pC);
            float AB  = (wA - wB).length();
            float AC  = (wA - wC).length();
            float BC  = (wB - wC).length();
            longestA = std::max(longestA, AB);
            longestA = std::max(longestA, AC);
            longestB = std::max(longestB, AB);
            longestB = std::max(longestB, BC);
            longestC = std::max(longestC, AC);
            longestC = std::max(longestC, BC);
        }

        VertexAttribute attrs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_SINT, asBytes(pixelCoords) },
            { "POSITION", 1, DXGI_FORMAT_R32_FLOAT,   asBytes(height) },
            { "POSITION", 2, DXGI_FORMAT_R32G32_SINT, asBytes(nextLodPixelCoords) },
            { "POSITION", 3, DXGI_FORMAT_R32_FLOAT,   asBytes(nextLodHeight) },
            { "POSITION", 4, DXGI_FORMAT_R32_FLOAT,   asBytes(longestEdge) },
        };

        return Mesh::generate(device, attrs, reinterpretSpan<const uint>(ib));
    }

    template <typename DEMesh>
    Mesh tipsifyMesh(const DEMesh &mesh)
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
        std::vector<int> indices;

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

        Mesh gpuMesh = this->gpuMesh(mesh, std::move(indices), vertexForNewIndex);

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

    void singleLod(Rect area, Mesh m)
    {
        setBounds(area);
        terrainLods.clear();
        terrainLods.resize(1);
        terrainLods.front().mesh = std::move(m);
    }

    void uniformGrid(Rect area)
    {
        Timer t;

        log("uniformGrid", "Generating uniform grid mesh with %d LODs\n",
            cfg_Settings.lodCount.get());

        setBounds(area);
        terrainLods.clear();

        for (int lod = 0; lod < cfg_Settings.lodCount; ++lod)
        {
            Timer lodTimer;

            int maxVertexCount = cfg_Settings.lodVertexCount(lod);
            int vertsPerSide = int(std::round(std::sqrt(float(maxVertexCount))));
            float2 vertexDistance = float2(area.size()) / float2(float(vertsPerSide - 1));
            int numVerts = vertsPerSide * vertsPerSide;

            std::vector<int2>  pixelCoords;
            std::vector<float> heights;
            std::vector<uint>  indices;

            pixelCoords.reserve(numVerts);
            heights.reserve(numVerts);
            indices.reserve(numVerts * 3);

            float2 invSize = 1.f / float2(heightmap->size);

            for (int y = 0; y < vertsPerSide; ++y)
            {
                for (int x = 0; x < vertsPerSide; ++x)
                {
                    int2 vertexGridCoords = int2(x, y);
                    float2 fCoords = float2(vertexGridCoords) * vertexDistance;
                    int2 texCoords = min(int2(round(fCoords)) + area.min, heightmap->size - 1);

                    float height = heightData.pixel<float>(uint2(texCoords));

                    pixelCoords.emplace_back(texCoords);
                    heights.emplace_back(height);
                }
            }

            for (int y = 0; y < vertsPerSide - 1; ++y)
            {
                for (int x = 0; x < vertsPerSide - 1; ++x)
                {
                    uint ul = y * vertsPerSide + x;
                    uint ur = ul + 1;
                    uint dl = ul + vertsPerSide;
                    uint dr = dl + 1;

                    if ((x + y) & 1)
                    {
                        indices.emplace_back(ul);
                        indices.emplace_back(dl);
                        indices.emplace_back(ur);
                        indices.emplace_back(dl);
                        indices.emplace_back(dr);
                        indices.emplace_back(ur);
                    }
                    else
                    {
                        indices.emplace_back(dl);
                        indices.emplace_back(dr);
                        indices.emplace_back(ul);
                        indices.emplace_back(dr);
                        indices.emplace_back(ur);
                        indices.emplace_back(ul);
                    }
                }
            }

            VertexAttribute attrs[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(pixelCoords) },
                { "POSITION", 1, DXGI_FORMAT_R32_FLOAT,    asBytes(heights) },
            };

            terrainLods.emplace_back();
            terrainLods.back().mesh = Mesh::generate(device, attrs, indices);

            log("uniformGrid", "    Generated LOD %d with %zu vertices and %zu indices in %.2f ms\n",
                cfg_Settings.lodCount - lod - 1,
                pixelCoords.size(),
                indices.size(),
                lodTimer.milliseconds());
        }

        std::reverse(terrainLods.begin(), terrainLods.end());

        log("uniformGrid", "Generated uniform grid mesh in %.2f ms\n",
            t.milliseconds());
    }

    void incrementalMaxError(Rect area, bool tipsify = true)
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
        using MB   = typename DErr::MeshBuffers;
        DErr mesh;

        Vert minBound = vertex(area, {0, 0});
        Vert maxBound = vertex(area, {1, 1});

        Random gen(95832);

        std::priority_queue<LargestError> largestError;
        std::vector<int> newTriangles;
        std::unordered_set<int2, PodHash, PodEqual> usedVertices;

        std::vector<MB> lods;
        std::vector<std::vector<Block32>> lodClusters;

        log("incrementalMaxError", "Generating incremental max error mesh with %d LODs\n",
            cfg_Settings.lodCount.get());

#if 0
        BowyerWatson<DErr> delaunay(mesh);
#else
        DelaunayFlip<DErr> delaunay(mesh);
#endif
        delaunay.superRectangle(minBound, maxBound);

        int numSuperVertices = mesh.numVertices();

        {
            int v0 = delaunay.insertVertex(vertex(area, { 1, 0 }));
            int v1 = delaunay.insertVertex(vertex(area, { 0, 1 }));
            int v2 = delaunay.insertVertex(vertex(area, { 0, 0 }));
            int v3 = delaunay.insertVertex(vertex(area, { 1, 1 }));

            for (int v : { v0, v1, v2, v3 })
            {
                usedVertices.emplace(int2(mesh.V(v).pos));

                mesh.vertexForEachTriangle(v, [&](int t)
                {
                    largestError.emplace(t);
                });
            }
        }

        XOR_ASSERT(!largestError.empty(), "No valid triangles to subdivide");

        for (int lod = 0; lod < cfg_Settings.lodCount; ++lod)
        {
            Timer lodTimer;

            int numLodVertices = cfg_Settings.lodVertexCount(lod);

            // Subtract from the vertex count to account for the superpolygon
            while (mesh.numVertices() - numSuperVertices < numLodVertices)
            {
                auto largest = largestError.top();
                largestError.pop();
                int t = largest.triangle;

                if (t < 0 || !mesh.triangleIsValid(t) || delaunay.triangleContainsSuperVertices(t))
                {
                    continue;
                }

                auto &triData = mesh.T(t);
                int3 verts = mesh.triangleVertices(t);
                Vert v0 = mesh.V(verts.x).pos;
                Vert v1 = mesh.V(verts.y).pos;
                Vert v2 = mesh.V(verts.z).pos;

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
                    constexpr int EdgeSamples = 0;

                    auto errorAt = [&](float3 bary)
                    {
                        float3 interpolated = interpolateBarycentric(float3(v0), float3(v1), float3(v2), bary);
                        Vert point = vertex(int2(interpolated));

                        float error = abs(float(point.z) - interpolated.z);
                        if (isPointInsideTriangleUnknownWinding(v0.vec2(), v1.vec2(), v2.vec2(), point.vec2())
                            && !usedVertices.count(int2(point))
                            && error > largestErrorFound)
                        {
                            largestErrorCoords = int2(point);
                            largestErrorFound = error;
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
                    triData.error = largestErrorFound;

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

            // Add extra vertices on area boundaries so the overall shape ends up being
            // a rectangle.
            {
                std::vector<std::pair<int2, int>> newBorderVerts;

                auto spv = delaunay.superPolygonVertices();
                for (int sv : spv.span())
                {
                    if (sv < 0)
                        continue;

                    for (int v : mesh.vertexAdjacentVertices(sv))
                    {
                        if (delaunay.isSuperPolygonVertex(v))
                            continue;

                        // Try all four outer edges, pick the closest one
                        int2 p = int2(mesh.V(v).pos);

                        int2 pTop = int2(p.x, int(minBound.y));
                        int2 pLeft = int2(int(minBound.x), p.y);
                        int2 pRight = int2(int(maxBound.x), p.y);
                        int2 pBottom = int2(p.x, int(maxBound.y));

                        int2 pEdge = pTop;
                        int minDistSq = (pEdge - p).lengthSqr();

                        for (auto &p_ : { pLeft, pRight, pBottom })
                        {
                            int dSq = (p_ - p).lengthSqr();
                            if (dSq < minDistSq)
                            {
                                pEdge = p_;
                                minDistSq = dSq;
                            }
                        }

                        if (!usedVertices.count(pEdge))
                        {
                            newBorderVerts.emplace_back(pEdge, v);
                        }
                    }
                }

                for (auto &v : newBorderVerts)
                    delaunay.insertVertexNearAnother(vertex(v.first), v.second);
            }

            lods.emplace_back(delaunay.exportWithoutSuperPolygon());

            if (tipsify)
            {
                auto clustered = clusterAndOptimize(lods.back().ib, 256);

                lods.back().ib = std::move(clustered.ib);

                optimizeVertexLocations(lods.back().vb,
                                        lods.back().ib);

                lodClusters.emplace_back(std::move(clustered.clusterSpans));
            }

            log("incrementalMaxError", "    Generated LOD %d with %zu vertices and %zu triangles in %.2f ms\n",
                cfg_Settings.lodCount - lod - 1,
                lods.back().vb.size(),
                lods.back().ib.size() / 3,
                lodTimer.milliseconds());
        }

        delaunay.removeSuperPolygon();
        mesh.vertexRemoveUnconnected();

        log("incrementalMaxError", "Generated incremental max error triangulation in %.2f ms\n",
            timer.milliseconds());


        setBounds(area);
        terrainLods.clear();
        for (size_t i = 0; i < lods.size(); ++i)
        {
            terrainLods.emplace_back();
            auto &lod     = terrainLods.back();

            lod.mesh      = gpuMeshFromBuffers(lods[i]);

            for (Block32 indices : lodClusters[i])
            {
                lod.clusters.emplace_back();
                auto &c   = lod.clusters.back();
                c.indices = indices;

                c.aabb.min = int2(std::numeric_limits<int>::max());
                c.aabb.max = int2(std::numeric_limits<int>::min());

                for (int k = c.indices.begin; k < c.indices.end; ++k)
                {
                    int idx = lods[i].ib[k];
                    auto &v = lods[i].vb[idx];
                    int2 pos = int2(v.pos);
                    c.aabb.min = min(c.aabb.min, pos);
                    c.aabb.max = max(c.aabb.max, pos);
                }
            }
        }

        std::reverse(terrainLods.begin(), terrainLods.end());
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
        int2 centered = pixelCoords - worldCenter;
        return float2(centered) * heightmap->texelSize;
    }

    void setBounds(Rect area)
    {
        this->area = area;
        float2 texels = float2(area.size());
        float2 size   = texels * heightmap->texelSize;

        worldHeight   = heightmap->maxHeight - heightmap->minHeight;
        worldDiameter = sqrt(size.lengthSqr() + worldHeight * worldHeight);

        worldMin = area.min;
        worldMax = area.max;
        worldCenter = area.min + area.size() / 2;
    }

    static float computeLODF(float distance)
    {
        float linearLOD        = distance / cfg_Settings.lodSwitchDistance;
        float logLOD           = cfg_Settings.isLinearLOD()
            ? linearLOD
            : (logf(linearLOD + 1) / logf(cfg_Settings.lodSwitchExponent));
        return logLOD;
    }

    static float inverseLOD(float LOD)
    {
        if (cfg_Settings.isLinearLOD())
            return LOD * cfg_Settings.lodSwitchDistance;
        else
            return cfg_Settings.lodSwitchDistance * std::max(0.f, std::powf(cfg_Settings.lodSwitchExponent, LOD) - 1);
    }

    static int computeLOD(float distance)
    {
        int lod = int(std::floor(computeLODF(distance)));

        return lod;
    }

    static int computeLOD(float distance, size_t numLODs)
    {
        return clamp(computeLOD(distance), 0, int(numLODs) - 1);
    }

    void selectLODs(CommandList &cmd, const float2 *cameraPos = nullptr)
    {
    }

    void render(CommandList &cmd,
                const float2 *cameraPos = nullptr,
                bool clustered = false) const
    {
        float LODSwitchNear[32] = { 0 };

        for (size_t i = 0; i < terrainLods.size() + 1; ++i)
        {
            LODSwitchNear[i] = inverseLOD(float(i));
        }
        // Never far cull the coarsest LOD
        LODSwitchNear[terrainLods.size()] = std::numeric_limits<float>::max();

        TerrainRendering::Constants constants;
        constants.heightmapInvSize  = float2(1.f) / float2(heightmap->size);
        constants.worldCenter       = float2(worldCenter);
        constants.worldMin          = worldMin;
        constants.worldMax          = worldMax;
        constants.texelSize         = heightmap->texelSize;
        constants.heightMin         = heightmap->minHeight;
        constants.heightMax         = heightmap->maxHeight;
        constants.lodLevel          = clamp(cfg_Settings.renderLod, 0, int(terrainLods.size() - 1));
        constants.vertexCullEnabled = uint(cfg_Settings.vertexCulling);

        constants.lodSwitchDistance = cfg_Settings.lodSwitchDistance;
        if (cfg_Settings.lodSwitchExponent > 1.05f)
            constants.lodSwitchExponentInvLog = 1 / logf(cfg_Settings.lodSwitchExponent);
        else
            constants.lodSwitchExponentInvLog = 0;

        if (cameraPos)
            constants.cameraWorldCoords = *cameraPos;

        constants.lodBias       = cfg_Settings.lodBias;

        auto &lod = terrainLods[constants.lodLevel];

        if (lod.clusters.empty() || !clustered)
        {
            constants.clusterId = -1;
            constants.vertexCullNear = LODSwitchNear[constants.lodLevel];
            constants.vertexCullFar  = LODSwitchNear[constants.lodLevel + 1];

            cmd.setConstants(constants);
            lod.mesh.setForRendering(cmd);

            cmd.drawIndexed(lod.mesh.numIndices());
        }
        else if (cfg_Settings.renderLod < 0 && cameraPos)
        {
            for (int i = int(terrainLods.size() - 1); i >= 0; --i)
            {
                auto &l = terrainLods[i];

                l.mesh.setForRendering(cmd);

                constants.lodLevel  = i;
                constants.clusterId = 0;

                constants.vertexCullNear = LODSwitchNear[i];
                constants.vertexCullFar  = LODSwitchNear[i + 1];
                
                for (auto &cluster : l.clusters)
                {
                    RectF aabb;

                    aabb.min = worldCoords(cluster.aabb.min);
                    aabb.max = worldCoords(cluster.aabb.max);

                    float2 clampedPos = clamp(*cameraPos, aabb.min, aabb.max);
                    float minDistance = (clampedPos - *cameraPos).length();

                    float2 corner0 = aabb.min;
                    float2 corner1 = aabb.min;
                    float2 corner2 = aabb.max;
                    float2 corner3 = aabb.max;

                    corner1.y = aabb.max.y;
                    corner2.y = aabb.min.y;

                    float maxDistance = 0;
                    for (auto &c : {corner0, corner1, corner2, corner3})
                        maxDistance = std::max(maxDistance, (c - *cameraPos).lengthSqr());

                    maxDistance = std::sqrt(maxDistance);

                    int minLOD = computeLOD(minDistance);
                    int maxLOD = computeLOD(maxDistance);

                    ++constants.clusterId;

                    if (minLOD > constants.lodLevel ||
                        maxLOD < constants.lodLevel)
                        continue;

                    cmd.setConstants(constants);
                    cmd.drawIndexed(uint(cluster.indices.size()), cluster.indices.begin);
                }
            }
        }
        else
        {
            lod.mesh.setForRendering(cmd);

            constants.clusterId = 0;

            constants.vertexCullNear = LODSwitchNear[constants.lodLevel];
            constants.vertexCullFar  = LODSwitchNear[constants.lodLevel + 1];
                
            for (auto &cluster : lod.clusters)
            {
                cmd.setConstants(constants);
                cmd.drawIndexed(uint(cluster.indices.size()), cluster.indices.begin);
                ++constants.clusterId;
            }
        }
    }

    info::InputLayoutInfo inputLayout()
    {
        return info::InputLayoutInfoBuilder()
            .element("POSITION", 0, DXGI_FORMAT_R32G32_SINT, 0)
            .element("POSITION", 1, DXGI_FORMAT_R32_FLOAT,   1)
            .element("POSITION", 2, DXGI_FORMAT_R32G32_SINT, 2)
            .element("POSITION", 3, DXGI_FORMAT_R32_FLOAT,   3)
            .element("POSITION", 4, DXGI_FORMAT_R32_FLOAT,   4)
            ;
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

    void computeAmbientOcclusion(SwapChain &sc,
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

        Random gen(120495);

        float radius = terrain->worldDiameter / 2;

        constexpr uint AOBitsPerPixel = 32;

        {
            uint i = 0;
            while (i < samples)
            {
                auto cmd = device.graphicsCommandList();
                auto e = cmd.profilingEvent("Ambient occlusion sampling");

                if (i == 0)
                {
                    terrain->selectLODs(cmd);
                    cmd.clearUAV(aoVisibilitySamples.uav);
                    cmd.clearUAV(aoVisibilityBits.uav);
                }

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

                e.done();
                device.execute(cmd);
            }
        }

        float maxVisibleSamples = float(samples);

        auto cmd = device.graphicsCommandList();
        auto e = cmd.profilingEvent("Ambient occlusion resolve");
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

        e.done();
        device.execute(cmd);
        device.waitUntilDrained();
    }

    void updateLighting()
    {
        lightingDefines.clear();

        if (heightmap->colorSRV)
            lightingDefines.emplace_back("TEXTURED");

        auto renderingMode = cfg_Settings.renderingMode;

        if (cfg_Settings.highlightCracks)
            lightingDefines.emplace_back("HIGHLIGHT_CRACKS");
        else if (renderingMode == RenderingMode::Lighting)
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
            terrain->render(cmd, &c.cameraPos2D, true);
        }

        cmd.setRenderTargets();
    }

    RenderTerrain::Constants computeConstants(TextureRTV &rtv,
                                              const Matrix &viewProj,
                                              const FPSCamera &camera)
    {
        RenderTerrain::Constants constants;

        float2 resolution = rtv.texture()->sizeFloat();

        float2 worldMin = terrain->worldCoords(terrain->worldMin);
        float2 worldMax = terrain->worldCoords(terrain->worldMax);

        float3 terrainMin = float3(worldMin.x, worldMin.y, heightmap->minHeight);
        float3 terrainMax = float3(worldMax.x, worldMax.y, heightmap->maxHeight);

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
        constants.cameraPos3D        = camera.position.s_xyz1;
        constants.cameraPos2D        = camera.position.s_xz;
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
                const FPSCamera &camera,
                bool wireframe = false)
    {
        updateLighting();

        RenderTerrain::Constants constants = computeConstants(rtv, viewProj, camera);

        terrain->selectLODs(cmd, &constants.cameraPos2D);

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

            terrain->render(cmd, &constants.cameraPos2D, true);
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
            if (cfg_Settings.highlightCracks)
                cmd.clearRTV(rtv, {1, 1, 1, 1});

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

            terrain->render(cmd, &constants.cameraPos2D, true);
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

            terrain->render(cmd, &constants.cameraPos2D, true);
        }

        cmd.setRenderTargets();
        prevViewProj = viewProj;
    }

    void visualize(CommandList &cmd, const FPSCamera &camera, float2 minCorner, float2 maxCorner)
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

        info::ShaderDefine defines[1];

        switch (mode)
        {
        case VisualizationMode::OnlyError:
        case VisualizationMode::WireframeError:
            defines[0] = info::ShaderDefine("SHOW_ERROR");
            break;
        case VisualizationMode::CPUError:
            defines[0] = info::ShaderDefine("CPU_ERROR");
            break;
        case VisualizationMode::LODLevel:
            defines[0] = info::ShaderDefine("LOD_LEVEL");
            break;
        case VisualizationMode::ContinuousLOD:
            defines[0] = info::ShaderDefine("CONTINUOUS_LOD");
            break;
        case VisualizationMode::ClusterId:
            defines[0] = info::ShaderDefine("CLUSTER_ID");
            break;
        default:
            break;
        }

        if (defines[0].define)
            cmd.bind(visualizeTriangulation.variant()
                     .pixelShader(info::SameShader {}, defines));
        else
            cmd.bind(visualizeTriangulation);

        cmd.setConstants(vtConstants);
        cmd.setShaderView(VisualizeTriangulation::heightMap,          heightmap->heightSRV);
        cmd.setShaderView(VisualizeTriangulation::cpuCalculatedError, terrain->cpuError);

        float2 cameraPos = camera.position.s_xz;
        terrain->render(cmd, &cameraPos, true);

        if (mode != VisualizationMode::OnlyHeight
            && mode != VisualizationMode::OnlyError
            && mode != VisualizationMode::ClusterId)
        {
            cmd.bind(visualizeTriangulation.variant()
                     .pixelShader(info::SameShader{}, { { "WIREFRAME" } })
                     .fill(D3D12_FILL_MODE_WIREFRAME));
            terrain->render(cmd, &cameraPos, true);
        }
    }
};

class TerrainPrototype : public Window
{
    XorLibrary xorLib;
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
    TriangulationMode triangulationMode = TriangulationMode::IncMaxError; //TriangulationMode::TiledUniformGrid;//TriangulationMode::UniformGrid;
    bool tipsifyMesh = true;
    bool blitArea    = true;
    bool blitNormal  = false;
    bool blitShadowMap = false;
    bool wireframe = false;
    bool largeVisualization = false;

    Terrain terrain;
    TerrainRenderer terrainRenderer;
public:
    TerrainPrototype()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
#if 0
        , xorLib(XorLibrary::DebugLayer::GPUBasedValidation)
#endif
    {
        xorLib.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);
#if 1
        device      = xorLib.defaultDevice();
#else
        device      = xorLib.warpDevice();
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
            terrain.uniformGrid(area);
            break;
        case TriangulationMode::IncMaxError:
            terrain.incrementalMaxError(area, tipsifyMesh);
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
            terrainRenderer.computeAmbientOcclusion(swapChain, waitForKey);

            log("Heightmap", "Generated ambient occlusion map in %.2f ms\n",
                aoTimer.milliseconds());
        }
    }

    void measureTerrain()
    {
#if 0
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
#endif
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

            ImGui::Separator();

            ImGui::Combo("Triangulation mode",
                         reinterpret_cast<int *>(&triangulationMode),
                         "Uniform grid\0"
                         "Incremental max error\0");
            ImGui::Checkbox("Tipsify vertex cache optimization", &tipsifyMesh);

            ImGui::Separator();

            ImGui::Checkbox("Show area", &blitArea);
            ImGui::Checkbox("Show normals", &blitNormal);
            ImGui::Checkbox("Show shadows", &blitShadowMap);
            ImGui::Checkbox("Wireframe", &wireframe);

            ImGui::Separator();

            configEnumImguiCombo("Visualize triangulation", terrainRenderer.mode);
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
                               camera,
                               wireframe);

        cmd.setRenderTargets(backbuffer, depthBuffer);

        {
            float2 max = float2(1590, 890);
            float2 min;
            if (largeVisualization)
                min = max - 800;
            else
                min = max - 300;

            terrainRenderer.visualize(cmd, camera,
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
        device.present(swapChain, cfg_Settings.vsync);
    }
};

int main(int argc, const char *argv[])
{
    return TerrainPrototype().run();
}
