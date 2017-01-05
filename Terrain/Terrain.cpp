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

#include <random>
#include <unordered_set>

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
    Quadric,
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

struct HeightmapRenderer
{
    using DE = DirectedEdge<Empty, int3>;

	Device device;
    GraphicsPipeline renderTerrain;
    GraphicsPipeline visualizeTriangulation;
    ComputePipeline computeNormalMapCS;
	Mesh mesh;
	Heightmap *heightmap = nullptr;
    ImageData heightData;
	float2 minWorld;
	float2 maxWorld;
    Rect area;
	float  maxErrorCoeff = .05f;
	VisualizationMode mode = VisualizationMode::WireframeHeight;
    TextureSRV cpuError;
    TextureSRV normalMap;
    TextureUAV normalMapUAV;
    struct LightingProperties
    {
        float3 sunDirection;
        float3 sunColor;
    };
    LightingProperties lighting;
    std::vector<info::ShaderDefine> lightingDefines;

	HeightmapRenderer() = default;
	HeightmapRenderer(Device device, Heightmap &hmap)
	{
		this->device = device;
		heightmap = &hmap;
        heightData = heightmap->height.imageData();

		uniformGrid(Rect::withSize(heightmap->size), 100);

        renderTerrain  = device.createGraphicsPipeline(
			GraphicsPipeline::Info()
			.vertexShader("RenderTerrain.vs")
			.pixelShader("RenderTerrain.ps")
			.depthMode(info::DepthMode::Write)
			.depthFormat(DXGI_FORMAT_D32_FLOAT)
			.renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
			.inputLayout(mesh.inputLayout()));

        visualizeTriangulation  = device.createGraphicsPipeline(
			GraphicsPipeline::Info()
			.vertexShader("VisualizeTriangulation.vs")
			.pixelShader("VisualizeTriangulation.ps")
			.renderTargetFormats(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
			.inputLayout(mesh.inputLayout()));

        computeNormalMapCS = device.createComputePipeline(
            ComputePipeline::Info("ComputeNormalMap.cs"));
        {
            normalMap = device.createTextureSRV(info::TextureInfoBuilder()
                                                //.size(uint2(512))
                                                .size(uint2(heightmap->size))
                                                .format(DXGI_FORMAT_R16G16B16A16_FLOAT)
                                                .uav());

            normalMapUAV = device.createTextureUAV(normalMap.texture());

            auto cmd = device.graphicsCommandList();
            computeNormalMap(cmd);
            device.execute(cmd);
        }
    }

    void computeNormalMap(CommandList &cmd)
    {
        cmd.bind(computeNormalMapCS);

        ComputeNormalMap::Constants constants;
        constants.size = normalMap.texture()->size;
        //constants.size = uint2(512);
        constants.axisMultiplier = float2(heightmap->texelSize);
        constants.heightMultiplier = 1.f;

        cmd.setConstants(constants);
        cmd.setShaderView(ComputeNormalMap::heightMap, heightmap->heightSRV);
        cmd.setShaderView(ComputeNormalMap::normalMap, normalMapUAV);

        cmd.dispatchThreads(ComputeNormalMap::threadGroupSize, uint3(constants.size));
    }

    void setLightingProperties(LightingProperties *props = nullptr)
    {
        lightingDefines.clear();

        if (props)
        {
            lighting = *props;
            if (heightmap->colorSRV)
                lightingDefines.emplace_back("TEXTURED");

            lightingDefines.emplace_back("LIGHTING");
        }
        else
        {
            memset(&lighting, 0, sizeof(lighting));
        }
    }

    template <typename DEMesh>
	void gpuMesh(const DEMesh &mesh, float2 minUV = float2(0, 0), float2 maxUV = float2(1, 1))
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

		this->mesh = Mesh::generate(device, attrs, indices);
	}

    template <typename DEMesh>
	void tipsifyMesh(const DEMesh &mesh, float2 minUV = float2(0, 0), float2 maxUV = float2(1, 1))
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

		this->mesh = Mesh::generate(device, attrs, indices);

        log("Heightmap", "Generated tipsified mesh with %d vertices and %d triangles in %.2f ms\n",
            mesh.numVertices(),
            mesh.numTriangles(),
            timer.milliseconds());
	}

    ErrorMetrics calculateMeshError()
    {
        Timer timer;

        RWImageData error(area.size(), DXGI_FORMAT_R32_FLOAT);
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
                error.pixel<float>(p - area.leftTop) = float(dz);
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
        float2 unnormalized = lerp(float2(area.leftTop), float2(area.rightBottom), uv);
        return vertex(int2(unnormalized));
    }

    void setBounds(Rect area)
    {
        this->area = area;
        float2 texels = float2(area.size());
        float2 size   = texels * heightmap->texelSize;
        float2 extent = size / 2.f;
        minWorld = -extent;
        maxWorld =  extent;
    }

    void uniformGrid(Rect area, uint quadsPerDim)
    {
        Timer t;

        area.rightBottom = min(area.rightBottom, heightmap->size);
        if (all(area.size() < uint2(128)))
            area.leftTop = area.rightBottom - 128;

        int2 sz        = int2(area.size());
        float2 szWorld = float2(sz) * heightmap->texelSize;

        int minDim = std::min(sz.x, sz.y);
        int vertexDistance = minDim / static_cast<int>(quadsPerDim);
        vertexDistance = std::max(1, vertexDistance);

        int2 verts = sz / vertexDistance;
        float2 fVerts = float2(verts);
        float2 fRes   = float2(sz);
        minWorld = -szWorld / 2.f;
        maxWorld = minWorld + szWorld;

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
                int2 texCoords = min(vertexGridCoords * vertexDistance + area.leftTop, heightmap->size - 1);
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

        this->mesh = Mesh::generate(device, attrs, indices);
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
                    float3 bary = uniformBarycentric(gen);
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

        setBounds(area);

        if (tipsify)
        {
            tipsifyMesh(mesh,
                        float2(area.leftTop) / float2(heightmap->size),
                        float2(area.rightBottom) / float2(heightmap->size));
        }
        else
        {
            gpuMesh(mesh,
                    float2(area.leftTop) / float2(heightmap->size),
                    float2(area.rightBottom) / float2(heightmap->size));
        }
	}

	void quadricSimplification(Rect area, uint vertices, bool tipsify = true)
    {
        Timer timer;

        setBounds(area);

        float2 areaSize = maxWorld - minWorld;

        float heightNormalization = 1.f / std::max(areaSize.x, areaSize.y);

        SimpleMesh groundTruth;

        int2 size = int2(area.size());
        float2 sizeF = float2(size);

        for (int y = area.leftTop.y; y <= area.rightBottom.y; ++y)
        {
            for (int x = area.leftTop.x; x <= area.rightBottom.x; ++x)
            {
                float z = heightData.pixel<float>(int2(x, y));
                float xNorm = static_cast<float>(x - area.leftTop.x) / sizeF.x;
                float yNorm = static_cast<float>(y - area.leftTop.y) / sizeF.y;
                float zNorm = z * heightNormalization;

                groundTruth.vertices.emplace_back(xNorm, yNorm, zNorm);
            }
        }

        int vertsPerRow = size.x + 1;
        for (int y = 0; y < size.y; ++y)
        {
            for (int x = 0; x < size.x; ++x)
            {
                int2 a = int2(x,     y);
                int2 b = int2(x,     y + 1);
                int2 c = int2(x + 1, y);
                int2 d = int2(x + 1, y + 1);

                int ia = a.y * vertsPerRow + a.x;
                int ib = b.y * vertsPerRow + b.x;
                int ic = c.y * vertsPerRow + c.x;
                int id = d.y * vertsPerRow + d.x;

                groundTruth.indices.emplace_back(ia);
                groundTruth.indices.emplace_back(ib);
                groundTruth.indices.emplace_back(ic);

                groundTruth.indices.emplace_back(ib);
                groundTruth.indices.emplace_back(id);
                groundTruth.indices.emplace_back(ic);
            }
        }

        log("Heightmap", "Ground truth mesh generated with %zu vertices and %zu triangles in %.2f ms\n",
            groundTruth.vertices.size(),
            groundTruth.indices.size() / 3,
            timer.milliseconds());

        SimpleMesh simplifiedMesh = quadricMeshSimplification(groundTruth,
                                                              vertices * 2);

        std::vector<float2> normalizedPos;
        std::vector<float>  heights;
        std::vector<float2> uvs;
        std::vector<uint>   indices;

        float2 minUV = float2(area.leftTop) / float2(heightmap->size);
        float2 maxUV = float2(area.rightBottom) / float2(heightmap->size);

        heightNormalization = 1.f / heightNormalization;

        for (auto &v : simplifiedMesh.vertices)
        {
            normalizedPos.emplace_back();
            auto &pos = normalizedPos.back();
            pos = float2(v);
            heights.emplace_back(v.z * heightNormalization);
            uvs.emplace_back(lerp(minUV, maxUV, pos));
        }

        indices = std::move(simplifiedMesh.indices);

        for (uint i = 0; i < indices.size(); i += 3)
        {
            uint a = indices[i];
            uint b = indices[i + 1];
            uint c = indices[i + 2];

            float2 pa = normalizedPos[a];
            float2 pb = normalizedPos[b];
            float2 pc = normalizedPos[c];

			bool ccw = !isTriangleCCW(normalizedPos[a], normalizedPos[b], normalizedPos[c]);

            if (!ccw)
                std::swap(indices[i + 1], indices[i + 2]);
        }

        log("Heightmap", "Generated quadric simplified triangulation with %zu vertices and %zu triangles in %.2f ms\n",
            normalizedPos.size(),
            indices.size() / 3,
            timer.milliseconds());

		VertexAttribute attrs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(normalizedPos) },
			{ "POSITION", 1, DXGI_FORMAT_R32_FLOAT,    asBytes(heights) },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, asBytes(uvs) },
		};

		this->mesh = Mesh::generate(device, attrs, indices);
    }

	void render(CommandList &cmd, const Matrix &viewProj, bool wireframe = false)
	{
        cmd.bind(renderTerrain.variant().pixelShader(info::SameShader {}, lightingDefines));

		RenderTerrain::Constants constants;
		constants.viewProj  = viewProj;
		constants.worldMin  = minWorld;
		constants.worldMax  = maxWorld;
        constants.heightMin = heightmap->minHeight;
        constants.heightMax = heightmap->maxHeight;

        RenderTerrain::LightingConstants lightingConstants;
        lightingConstants.sunDirection = lighting.sunDirection.s_xyz0;
        lightingConstants.sunColor     = lighting.sunColor.s_xyz0;

        cmd.setConstants(constants);
        cmd.setConstants(lightingConstants);
        mesh.setForRendering(cmd);
        cmd.setShaderView(RenderTerrain::terrainColor,  heightmap->colorSRV);
        cmd.setShaderView(RenderTerrain::terrainNormal, normalMap);
        {
            auto p = cmd.profilingEvent("Draw opaque");
            cmd.drawIndexed(mesh.numIndices());
        }

        if (wireframe)
        {
            auto p = cmd.profilingEvent("Draw wireframe");
            cmd.bind(renderTerrain.variant()
                     .pixelShader(info::SameShader {}, { { "WIREFRAME" } })
                     .depthMode(info::DepthMode::ReadOnly)
                     .depthBias(10000)
                     .fill(D3D12_FILL_MODE_WIREFRAME));
            cmd.setShaderView(RenderTerrain::terrainColor, heightmap->colorSRV);
            cmd.setShaderView(RenderTerrain::terrainNormal, normalMap);
            cmd.setConstants(constants);
            cmd.setConstants(lightingConstants);
            cmd.drawIndexed(mesh.numIndices());
        }
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

		mesh.setForRendering(cmd);

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
		cmd.setShaderView(VisualizeTriangulation::cpuCalculatedError, cpuError);
		cmd.drawIndexed(mesh.numIndices());

		if (mode == VisualizationMode::WireframeHeight || mode == VisualizationMode::WireframeError)
		{
			cmd.bind(visualizeTriangulation.variant()
					 .pixelShader(info::SameShader{}, { { "WIREFRAME" } })
					 .fill(D3D12_FILL_MODE_WIREFRAME));
			cmd.setConstants(vtConstants);
			cmd.setShaderView(VisualizeTriangulation::heightMap, heightmap->heightSRV);
            cmd.setShaderView(VisualizeTriangulation::cpuCalculatedError, cpuError);
			cmd.drawIndexed(mesh.numIndices());
		}
	}
};

class Terrain : public Window
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
    struct Lighting
    {
        bool enabled = false;
        Angle sunAzimuth   = Angle::degrees(45.f);
        Angle sunElevation = Angle::degrees(45.f);
        float sunIntensity = 1.f;
    } lighting;
    TriangulationMode triangulationMode = TriangulationMode::IncMaxError;//TriangulationMode::UniformGrid;
    bool tipsifyMesh = true;
    bool blitArea    = true;
    bool blitNormal  = false;
    bool wireframe = false;
    bool largeVisualization = false;

	HeightmapRenderer heightmapRenderer;
public:
    Terrain()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
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

		heightmapRenderer = HeightmapRenderer(device, heightmap);

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
            heightmapRenderer.uniformGrid(area, quadsPerDim());
            break;
        case TriangulationMode::IncMaxError:
            heightmapRenderer.incrementalMaxError(area, vertexCount(), tipsifyMesh);
            break;
        case TriangulationMode::Quadric:
            heightmapRenderer.quadricSimplification(area, vertexCount(), tipsifyMesh);
            break;
        }

        heightmapRenderer.calculateMeshError();

        camera.position = float3(0, heightmap.maxHeight + NearPlane * 10, 0);
    }

    void updateLighting()
    {
        if (lighting.enabled)
        {
            HeightmapRenderer::LightingProperties props = {};
            auto M = Matrix::azimuthElevation(lighting.sunAzimuth, lighting.sunElevation);
            props.sunDirection = normalize(float3(M.transform(float3(0, 0, -1))));
            props.sunColor     = float3(1) * lighting.sunIntensity;

            heightmapRenderer.setLightingProperties(&props);
        }
        else
        {
            heightmapRenderer.setLightingProperties();
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
            heightmapRenderer.uniformGrid(area, quadsPerDim(d));
            uni[d] = heightmapRenderer.calculateMeshError();
            heightmapRenderer.incrementalMaxError(area, vertexCount(d), true);
            inc[d] = heightmapRenderer.calculateMeshError();
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

            if (ImGui::Checkbox("Lighting", &lighting.enabled))
                updateLighting();
            if (ImGui::SliderFloat("Sun azimuth",   &lighting.sunAzimuth.radians, 0, 2 * Pi))
                updateLighting();
            if (ImGui::SliderFloat("Sun elevation", &lighting.sunElevation.radians, 0, Pi / 2.f))
                updateLighting();

            ImGui::Combo("Triangulation mode",
                         reinterpret_cast<int *>(&triangulationMode),
                         "Uniform grid\0"
                         "Incremental max error\0"
                         "Quadric\0");
            ImGui::Checkbox("Tipsify vertex cache optimization", &tipsifyMesh);
            ImGui::Checkbox("Show area", &blitArea);
            ImGui::Checkbox("Show normals", &blitNormal);
            ImGui::Checkbox("Wireframe", &wireframe);
			ImGui::Combo("Visualize triangulation",
						 reinterpret_cast<int *>(&heightmapRenderer.mode),
						 "Disabled\0"
						 "WireframeHeight\0"
						 "OnlyHeight\0"
						 "WireframeError\0"
						 "OnlyError\0"
						 "CPUError\0");
            ImGui::Checkbox("Large visualization", &largeVisualization);
            ImGui::SliderFloat("Error magnitude", &heightmapRenderer.maxErrorCoeff, 0, .25f);

            if (ImGui::Button("Update"))
                updateTerrain();

            if (ImGui::Button("Measurement"))
                measureTerrain();
        }
        ImGui::End();

        if (0) {
            auto area = Rect::withSize(areaStart, areaSize);
            static int V = 4;
            heightmapRenderer.incrementalMaxError(area, V++);
        }

        {
            auto p = cmd.profilingEvent("Clear");
            cmd.clearRTV(backbuffer, float4(0, 0, 0, 1));
            cmd.clearDSV(depthBuffer, 0);
        }

        cmd.setRenderTargets(backbuffer, depthBuffer);

		heightmapRenderer.render(cmd, 
							 Matrix::projectionPerspective(backbuffer.texture()->size, math::DefaultFov,
														   1.f, heightmap.worldSize.x * 1.5f)
							 * camera.viewMatrix(),
							 wireframe);

        {
            float2 rightBottom = float2(1590, 890);
            float2 leftTop;
            if (largeVisualization)
                leftTop = rightBottom - 800;
            else
                leftTop = rightBottom - 300;

            heightmapRenderer.visualize(cmd,
                                        remap(float2(0), float2(backbuffer.texture()->size), float2(-1, 1), float2(1, -1), leftTop),
                                        remap(float2(0), float2(backbuffer.texture()->size), float2(-1, 1), float2(1, -1), rightBottom));
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
                      heightmapRenderer.normalMap, Rect::withSize(areaStart, areaSize),
                      float4(0.5f, 0.5f, 1, 1), float4(0.5f, 0.5f, 0, 1));
        }

#if 0
        blit.blit(cmd,
            backbuffer, Rect(int2(100), int2(800)),
            heightmapRenderer.normalMap, Rect::withSize(areaStart, areaSize));
#endif

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);
        device.present(swapChain);

#if 0
        while ((GetAsyncKeyState(VK_SPACE) & 0x8000)) { pumpMessages(); Sleep(1); }
        while (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) { pumpMessages(); Sleep(1); }
#endif
    }
};

int main(int argc, const char *argv[])
{
    return Terrain().run();
}
