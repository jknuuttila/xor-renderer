#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"
#include "Xor/Blit.hpp"
#include "Xor/Mesh.hpp"
#include "Xor/ProcessingMesh.hpp"
#include "Xor/DirectedEdge.hpp"

#include "RenderTerrain.sig.h"
#include "VisualizeTriangulation.sig.h"

#include <random>
#include <unordered_set>


using namespace xor;

static const float ArcSecond = 30.87f;

static const float NearPlane = 1.f;

struct Heightmap
{
    Image image;
    TextureSRV srv;
    int2 size;
    float2 worldSize;
    float texelSize;
    float minHeight = 1e10;
    float maxHeight = -1e10;

    Heightmap() = default;
    Heightmap(Device &device, StringView file, float texelSize = ArcSecond / 3.f)
    {
        image = Image(Image::Builder().filename(file));
        srv   = device.createTextureSRV(Texture::Info(image));
        size  = int2(image.size());
        this->texelSize = texelSize;
        worldSize = texelSize * float2(size);

#if defined(_DEBUG)
        minHeight = 340.f;
        maxHeight = 2600.f;
#else
        Timer t;
        auto size = image.size();
        auto sr   = image.imageData();
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

};

enum class TriangulationMode
{
    UniformGrid,
    Random,
    IncMinError,
};

enum class VisualizationMode
{
	Disabled,
	WireframeHeight,
	OnlyHeight,
	WireframeError,
	OnlyError,
};

struct HeightmapRenderer
{
    using DE = DirectedEdge<>;

	Device device;
    GraphicsPipeline renderTerrain;
    GraphicsPipeline visualizeTriangulation;
	Mesh mesh;
	Heightmap *heightmap = nullptr;
    ImageData heightData;
	float2 minWorld;
	float2 maxWorld;
	float  maxErrorCoeff = .05f;
	VisualizationMode mode = VisualizationMode::WireframeHeight;

	HeightmapRenderer() = default;
	HeightmapRenderer(Device device, Heightmap &hmap)
	{
		this->device = device;
		heightmap = &hmap;
        heightData = heightmap->image.imageData();

		uniformGrid(Rect::withSize(heightmap->size), 100);
		// randomTriangulation(Rect::withSize(heightmap->size), 500);

#if 0
        auto il = info::InputLayoutInfoBuilder()
            .element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT)
            .element("POSITION", 1, DXGI_FORMAT_R32_FLOAT)
            .element("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT);
#endif

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
    }

    template <typename DEMesh>
	void gpuMesh(const DEMesh &mesh, float2 minUV = float2(0, 0), float2 maxUV = float2(1, 1))
	{
        auto verts    = mesh.vertices();
		auto numVerts = verts.size();
		std::vector<float2> normalizedPos(numVerts);
		std::vector<float>  height(numVerts);
		std::vector<float2> uv(numVerts);

		for (uint i = 0; i < numVerts; ++i)
		{
			auto &v          = verts[i];
			uv[i]            = float2(v.pos);
			normalizedPos[i] = remap(minUV, maxUV, float2(0), float2(1), float2(v.pos));
			height[i]        = v.pos.z;
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

    float3 vertex(Rect area, float2 uv)
    {
        float2 unnormalized = lerp(float2(area.leftTop), float2(area.rightBottom), uv);
        float2 normalized   = unnormalized / float2(heightmap->size);

        float height = heightData.pixel<float>(uint2(unnormalized));

        return float3(normalized.x, normalized.y, height);
    }

    void setBounds(Rect area)
    {
        float2 texels = float2(area.size());
        float2 size   = texels * heightmap->texelSize;
        float2 extent = size / 2.f;
        minWorld = -extent;
        maxWorld =  extent;
    }

    void uniformGrid(Rect area, uint vertices)
    {
        int density = static_cast<int>(round(sqrt(static_cast<double>(vertices))));

        Timer t;

        area.rightBottom = min(area.rightBottom, heightmap->size);
        if (all(area.size() < uint2(128)))
            area.leftTop = area.rightBottom - 128;

        int2 sz        = int2(area.size());
        float2 szWorld = float2(sz) * heightmap->texelSize;

        int minDim = std::min(sz.x, sz.y);
        int vertexDistance = minDim / density;
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

	void randomTriangulation(Rect area, uint vertices)
	{
		Timer timer;

		DE mesh;
        int first  = mesh.addTriangle(vertex(area, {1, 0}), vertex(area, {0, 1}), vertex(area, {0, 0}));
        int second = mesh.addTriangleToBoundary(mesh.triangleEdge(first), vertex(area, {1, 1}));

		std::mt19937 gen(12345);

        BowyerWatson<DE> delaunay(mesh);

		while (mesh.numVertices() < static_cast<int>(vertices))
		{
            int triangle = -1;
            float largestArea = 0;

            for (int j = 0; j < 10; ++j)
            {
                int t = std::uniform_int_distribution<int>(0u, mesh.numTriangles() - 1)(gen);
                if (!mesh.triangleIsValid(t))
                    continue;
                int3 verts = mesh.triangleVertices(t);

                float2 v0 = float2(mesh.V(verts.x).pos);
                float2 v1 = float2(mesh.V(verts.y).pos);
                float2 v2 = float2(mesh.V(verts.z).pos);
                float area = abs(triangleDoubleSignedArea(v0, v1, v2));

                if (area > largestArea)
                {
                    triangle    = t;
                    largestArea = area;
                }
            }

            if (triangle < 0 || !mesh.triangleIsValid(triangle))
                continue;

			float3 bary = uniformBarycentric(gen);
            int3 vs = mesh.triangleVertices(triangle);
            float3 pos = interpolateBarycentric(mesh.V(vs.x).pos,
                                                mesh.V(vs.y).pos,
                                                mesh.V(vs.z).pos,
                                                bary);
            pos.z = heightData.pixel<float>(float2(pos));

            delaunay.insertVertex(triangle, pos);
		}

        log("Heightmap", "Generated random triangulation with %d vertices and %d triangles in %.2f ms\n",
            mesh.numVertices(),
            mesh.numTriangles(),
            timer.milliseconds());

        setBounds(area);
		gpuMesh(mesh,
                float2(area.leftTop) / float2(heightmap->size),
                float2(area.rightBottom) / float2(heightmap->size));
	}

	void incrementalMinError(Rect area, uint vertices)
	{
		Timer timer;

        struct TriangleError
        {
            float3 bary;
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

        using DErr = DirectedEdge<TriangleError>;
		DErr mesh;

        float3 minBound = vertex(area, {0, 0});
        float3 maxBound = vertex(area, {1, 1});

        std::mt19937 gen(95832);

        std::priority_queue<LargestError> largestError;
        std::unordered_set<int> knownTriangles;
        std::vector<int> newTriangles;

#if 1
        // BowyerWatson<DErr> delaunay(mesh);
        DelaunayFlip<DErr> delaunay(mesh);
        delaunay.superTriangle(float2(minBound), float2(maxBound));

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
#elif 0
        DelaunayFlip<DErr> delaunay(mesh);
        int first  = mesh.addTriangle(vertex(area, {1, 0}), vertex(area, {0, 1}), vertex(area, {0, 0}));
        int second = mesh.addTriangleToBoundary(mesh.triangleEdge(first), vertex(area, {1, 1}));
        largestError.emplace(first);
        largestError.emplace(second);
#endif

        XOR_ASSERT(!largestError.empty(), "No valid triangles to subdivide");

        // Subtract 3 from the vertex count to account for the supertriangle
		while (mesh.numVertices() - 3 < static_cast<int>(vertices))
		{
            auto largest = largestError.top();
            largestError.pop();
            int t = largest.triangle;

            if (t < 0 || !mesh.triangleIsValid(t) || delaunay.triangleContainsSuperVertices(t)
                )
            {
                print("Triangle %d is invalid, skipping\n", t);
                continue;
            }

            auto &triData = mesh.T(t);
            int3 verts  = mesh.triangleVertices(t);
            float3 v0   = mesh.V(verts.x).pos;
            float3 v1   = mesh.V(verts.y).pos;
            float3 v2   = mesh.V(verts.z).pos;

            // If the error isn't known, estimate it
            if (!largest.error || largest.error != triData.error)
            {
                float3 largestErrorBary;
                float largestErrorFound = -1;
                print("Estimating error for triangle %d\n", t);
                print("    V0: (%f %f %f)\n", v0.x, v0.y, v0.z);
                print("    V1: (%f %f %f)\n", v1.x, v1.y, v1.z);
                print("    V2: (%f %f %f)\n", v2.x, v2.y, v2.z);

                constexpr int InteriorSamples = 5;
                constexpr int EdgeSamples     = 100;

                auto errorAt = [&] (float3 bary)
                {
                    float3 interpolated = interpolateBarycentric(v0, v1, v2, bary);
                    float  correctZ     = heightData.pixel<float>(float2(interpolated));

                    float error = abs(correctZ - interpolated.z);
                    if (error > largestErrorFound)
                    {
                        print("    (%.3f %.3f %.3f): abs(%f - %f) = %f > %f\n",
                              bary.x, bary.y, bary.z,
                              correctZ, interpolated.z, error, largestErrorFound);
                        largestErrorBary  = bary;
                        largestErrorFound = error;
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

                triData.bary  = largestErrorBary;
                triData.error = largestErrorFound;

                largestError.emplace(t, largestErrorFound);
                print("Triangle %d error: %f at (%.3f %.3f %.3f)\n",
                      t, triData.error,
                      triData.bary.x, triData.bary.y, triData.bary.z);
            }
            // The error is known, and it was the largest, so insert a new vertex
            // in that position.
            else
            {
                auto bary = triData.bary;
                float3 newVertex = interpolateBarycentric(v0, v1, v2, bary);
                newVertex.z      = heightData.pixel<float>(float2(newVertex));
                newTriangles.clear();
                delaunay.insertVertex(t, newVertex, &newTriangles);
                print("Inserted new vertex (%f %f %f) in triangle %d (%.3f %.3f %.3f)\n",
                      newVertex.x,
                      newVertex.y,
                      newVertex.z,
                      t,
                      bary.x,
                      bary.y,
                      bary.z);

                knownTriangles.erase(t);
                print("New triangles: ");
                for (int nt : newTriangles)
                {
                    //if (!knownTriangles.count(nt))
                    {
                        print("%d, ", nt);
                        largestError.emplace(nt);
                        knownTriangles.emplace(nt);
                    }
                }
                print("\n");
            }
		}

        delaunay.removeSuperTriangle();

        log("Heightmap", "Generated incremental min error triangulation with %d vertices and %d triangles in %.2f ms\n",
            mesh.numVertices(),
            mesh.numTriangles(),
            timer.milliseconds());

        setBounds(area);
		gpuMesh(mesh,
                float2(area.leftTop) / float2(heightmap->size),
                float2(area.rightBottom) / float2(heightmap->size));
	}

	void render(CommandList &cmd, const Matrix &viewProj, bool wireframe = false)
	{
        cmd.bind(renderTerrain);

		RenderTerrain::Constants constants;
		constants.viewProj  = viewProj;
		constants.worldMin  = minWorld;
		constants.worldMax  = maxWorld;
        constants.heightMin = heightmap->minHeight;
        constants.heightMax = heightmap->maxHeight;

        cmd.setConstants(constants);
        mesh.setForRendering(cmd);
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
            cmd.setConstants(constants);
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
		else
			cmd.bind(visualizeTriangulation);

		cmd.setConstants(vtConstants);
		cmd.setShaderView(VisualizeTriangulation::heightMap, heightmap->srv);
		cmd.drawIndexed(mesh.numIndices());

		if (mode == VisualizationMode::WireframeHeight || mode == VisualizationMode::WireframeError)
		{
			cmd.bind(visualizeTriangulation.variant()
					 .pixelShader(info::SameShader{}, { { "WIREFRAME" } })
					 .fill(D3D12_FILL_MODE_WIREFRAME));
			cmd.setConstants(vtConstants);
			cmd.setShaderView(VisualizeTriangulation::heightMap, heightmap->srv);
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
    int areaSize  = 2048;
	int triangulationDensity = 6;
    TriangulationMode triangulationMode = TriangulationMode::IncMinError;//TriangulationMode::UniformGrid;
    int vertexCount = -1;
    bool blitArea  = true;
    bool wireframe = false;

	HeightmapRenderer heightmapRenderer;
public:
    Terrain()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

        device    = xor.defaultDevice();
        swapChain = device.createSwapChain(*this);
        depthBuffer = device.createTextureDSV(Texture::Info(size(), DXGI_FORMAT_D32_FLOAT));
        blit = Blit(device);

        Timer loadingTime;

        heightmap = Heightmap(device, XOR_DATA "/heightmaps/grand-canyon/floatn36w114_13.flt");
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
        vertexCount = 1 << triangulationDensity;
        //vertexCount = triangulationDensity;

        switch (triangulationMode)
        {
        case TriangulationMode::UniformGrid:
        default:
            heightmapRenderer.uniformGrid(area, vertexCount);
            break;
        case TriangulationMode::Random:
            heightmapRenderer.randomTriangulation(area, vertexCount);
            break;
        case TriangulationMode::IncMinError:
            heightmapRenderer.incrementalMinError(area, vertexCount);
            break;
        }

        camera.position = float3(0, heightmap.maxHeight + NearPlane * 10, 0);
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
            ImGui::SliderInt2("Start", areaStart.data(), 0, heightmap.size.x - areaSize);
            ImGui::SliderInt("Density", &triangulationDensity, 5, 16);
            vertexCount = 1 << triangulationDensity;
            ImGui::Text("Vertex count: %d", vertexCount);
            ImGui::Combo("Triangulation mode",
                         reinterpret_cast<int *>(&triangulationMode),
                         "Uniform grid\0"
                         "Random\0"
                         "Incremental min error\0");
            ImGui::Checkbox("Show area", &blitArea);
            ImGui::Checkbox("Wireframe", &wireframe);
			ImGui::Combo("Visualize triangulation",
						 reinterpret_cast<int *>(&heightmapRenderer.mode),
						 "Disabled\0"
						 "WireframeHeight\0"
						 "OnlyHeight\0"
						 "WireframeError\0"
						 "OnlyError\0");
            ImGui::SliderFloat("Error magnitude", &heightmapRenderer.maxErrorCoeff, 0, .25f);

            if (ImGui::Button("Update"))
                updateTerrain();

            ImGui::End();
        }

        if (0) {
            auto area = Rect::withSize(areaStart, areaSize);
            static int V = 2;
            heightmapRenderer.incrementalMinError(area, V++);
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

		heightmapRenderer.visualize(cmd,
								remap(float2(0), float2(backbuffer.texture()->size), float2(-1, 1), float2(1, -1), float2(1110, 410)),
							    remap(float2(0), float2(backbuffer.texture()->size), float2(-1, 1), float2(1, -1), float2(1590, 890)));

        cmd.setRenderTargets();

        if (blitArea)
        {
            auto p = cmd.profilingEvent("Blit heightmap");
            float2 norm = normalizationMultiplyAdd(heightmap.minHeight, heightmap.maxHeight);

            blit.blit(cmd,
                      backbuffer, Rect::withSize(int2(backbuffer.texture()->size - 300).s_x0, 300),
                      heightmap.srv, Rect::withSize(areaStart, areaSize),
                      norm.s_x000, norm.s_y001);
        }

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
