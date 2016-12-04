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
        auto sr   = image.subresource(0);
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

enum class VisualizationMode
{
	Disabled,
	WireframeHeight,
	OnlyHeight,
	WireframeError,
	OnlyError,
};

template <typename DE>
class BowyerWatson
{
    // Mesh that this algorithm operates on
    DE &mesh;
    // Triangles that have already been checked for circumcircle violations
    // during the current insertion.
    std::unordered_set<int> trisExplored;
    // Triangles that will be checked for circumcircle violations.
    std::unordered_set<int> trisToExplore;
    std::unordered_set<int> removedEdges;
    std::unordered_set<int> removedTriangles;
    std::vector<int3> removedBoundary;
    std::unordered_map<int, int> vertexNeighbors;
public:
    BowyerWatson(DE &mesh) : mesh(mesh) {}

    void insertVertex(int containingTriangle, float3 newVertexPos)
    {
        removedTriangles.clear();
        removedEdges.clear();
        trisToExplore.clear();
        trisExplored.clear();
        trisToExplore.insert(containingTriangle);

        print("\nRemoving triangles\n");
        while (!trisToExplore.empty())
        {
            int tri = *trisToExplore.begin();

            bool removeTriangle = trisExplored.empty();

            trisToExplore.erase(tri);
            trisExplored.insert(tri);
            mesh.XOR_DE_DEBUG_TRIANGLE(tri, "Checking");

            // The first triangle is the triangle we placed the vertex in,
            // which will be removed by definition. We thus don't check the
            // circumcircle to avoid numerical errors.
            if (!removeTriangle)
            {
                int3 verts = mesh.triangleVertices(tri);

                float2 v0 = float2(mesh.V(verts.x).pos);
                float2 v1 = float2(mesh.V(verts.y).pos);
                float2 v2 = float2(mesh.V(verts.z).pos);

                float2 inside = (v0 + v1 + v2) / 3.f;

                float insideSign = pointsOnCircle(v0, v1, v2, inside);
                float posSign    = pointsOnCircle(v0, v1, v2, float2(newVertexPos));
                print("Circumcircle test (inside): %g vs %g\n", insideSign, posSign);

                // If the signs are the same, the product will be non-negative.
                // This means that pos is inside the circumcircle.
                removeTriangle = insideSign * posSign > 0;

                // Do not remove triangles for which the vertex is extremely close
                // to being on the circumcircle. This usually happens because of
                // small triangles and numerical instability, and leads to problems
                // with the removed area not being well-behaved anymore.
                constexpr float Epsilon = 1e-5f;
                if (abs(posSign) < Epsilon)
                    removeTriangle = false;
            }

            if (removeTriangle)
            {
                int3 edges = mesh.triangleAllEdges(tri);

                mesh.XOR_DE_DEBUG_TRIANGLE(tri, "Removing");
                removedTriangles.insert(tri);
                for (int e : edges.span())
                {
                    mesh.XOR_DE_DEBUG_EDGE(e);
                    removedEdges.insert(e);
                    int n = mesh.edgeNeighbor(e);
                    if (n >= 0)
                    {
                        int tn = mesh.edgeTriangle(n);
                        if (!trisExplored.count(tn))
                            trisToExplore.insert(tn);
                    }
                }
            }
        }

        int newVertex = mesh.addVertex(newVertexPos);
        vertexNeighbors.clear();
        removedBoundary.clear();

        XOR_ASSERT(!removedEdges.empty(), "Each new vertex should delete at least one triangle");

        print("\nDetermining boundary\n");
        for (int e : removedEdges)
        {
            int n = mesh.edgeNeighbor(e);
            if (n < 0 || !removedEdges.count(n))
            {
                print("Boundary ");
                mesh.XOR_DE_DEBUG_EDGE(e);
                removedBoundary.emplace_back(mesh.edgeStart(e),
                                             mesh.edgeTarget(e),
                                             mesh.edgeNeighbor(e));
            }
            else
            {
                print("Inside ");
                mesh.XOR_DE_DEBUG_EDGE(e);
            }
        }

        print("\nChecking boundary validity\n");
        {
            std::unordered_map<int, int> startVerts;
            std::unordered_map<int, int> targetVerts;

            std::unordered_map<int, int> vertPath;
            for (int3 stn : removedBoundary)
            {
                ++startVerts[stn.x];
                ++targetVerts[stn.y];
                vertPath[stn.x] = stn.y;
            }

            for (auto &kv : startVerts)
                XOR_ASSERT(kv.second == 1, "Vertices must form a closed loop");
            for (auto &kv : targetVerts)
                XOR_ASSERT(kv.second == 1, "Vertices must form a closed loop");

            size_t count = 0;
            int first = vertPath.begin()->first;
            int v = first;
            for (;;)
            {
                print("%d -> ", v);
                auto it = vertPath.find(v);
                XOR_ASSERT(it != vertPath.end(), "Vertices must form a closed loop");
                ++count;
                v = it->second;

                if (v == first)
                    break;
            }
            print("\n");

            XOR_ASSERT(count == vertPath.size(), "Vertices must form a closed loop");
        }

        print("\nRetriangulating\n");

        for (auto stn : removedBoundary)
        {
            int3 vs;
            vs.x = stn.x;
            vs.y = stn.y;
            vs.z = newVertex;

            int newTriangle = mesh.addTriangle(vs.x, vs.y, vs.z);
            int3 es = mesh.triangleAllEdges(newTriangle);

            int n = stn.z;
            mesh.edgeUpdateNeighbor(es.x, n);

            auto updateVertexNeighbors = [&](int vert, int edge)
            {
                auto it = vertexNeighbors.find(vert);
                if (it == vertexNeighbors.end())
                {
                    vertexNeighbors.insert(it, { vert, edge });
                }
                else
                {
                    mesh.edgeUpdateNeighbor(edge, it->second);
                }
            };

            updateVertexNeighbors(vs.y, es.y);
            updateVertexNeighbors(vs.x, es.z);

            mesh.XOR_DE_DEBUG_TRIANGLE(newTriangle, "Creating");
        }

        for (int tri : removedTriangles)
        {
            mesh.disconnectTriangle(tri);
            mesh.removeTriangle(tri);
        }
    }
};

struct HeightmapRenderer
{
    using DE = DirectedEdge<>;

	Device device;
    GraphicsPipeline renderTerrain;
    GraphicsPipeline visualizeTriangulation;
	Mesh mesh;
	Heightmap *heightmap = nullptr;
	float2 minWorld;
	float2 maxWorld;
	float  maxErrorCoeff = .05f;
	VisualizationMode mode = VisualizationMode::WireframeHeight;

	HeightmapRenderer() = default;
	HeightmapRenderer(Device device, Heightmap &hmap)
	{
		this->device = device;
		heightmap = &hmap;

		// uniformGrid(Rect::withSize(heightmap->size), -2);
		randomTriangulation(Rect::withSize(heightmap->size), 300);

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

	void gpuMesh(const DE &mesh)
	{
        auto verts    = mesh.vertices();
		auto numVerts = verts.size();
		std::vector<float2> normalizedPos(numVerts);
		std::vector<float>  height(numVerts);
		std::vector<float2> uv(numVerts);

		for (uint i = 0; i < numVerts; ++i)
		{
			auto &v          = verts[i];
			normalizedPos[i] = float2(v.pos);
			height[i]        = v.pos.z;
			uv[i]            = normalizedPos[i];
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

	void randomTriangulation(Rect area, uint vertices)
	{
		Timer timer;

		DE mesh;
        int first  = mesh.addTriangle({1, 0, 0}, {0, 1, 0}, {0, 0, 0});
        int second = mesh.addTriangleToBoundary(mesh.triangleEdge(first), {1, 1, 0});

		std::mt19937 gen(12345);

        BowyerWatson<DE> delaunay(mesh);

		while (mesh.numVertices() < static_cast<int>(vertices))
		{
            int triangle;
            float largestArea = 0;

            for (int j = 0; j < 10; ++j)
            {
                int t = std::uniform_int_distribution<int>(0u, mesh.numTriangles() - 1)(gen);
                if (!mesh.triangleIsValid(t))
                    continue;
                int3 verts = mesh.triangleVertices(t);
                float area = abs(triangleDoubleSignedArea(float2(mesh.V(verts.x).pos),
                                                          float2(mesh.V(verts.y).pos),
                                                          float2(mesh.V(verts.z).pos)));
                if (area > largestArea)
                {
                    triangle    = t;
                    largestArea = area;
                }
            }

            if (!mesh.triangleIsValid(triangle))
                continue;

			float3 bary = uniformBarycentric(gen);
            int3 vs = mesh.triangleVertices(triangle);
            float3 pos =
                mesh.V(vs.x).pos * bary.x +
                mesh.V(vs.y).pos * bary.y +
                mesh.V(vs.z).pos * bary.z;

            delaunay.insertVertex(triangle, pos);
		}

        log("Heightmap", "Generated random triangulation with %d vertices and %d triangles in %.2f ms\n",
            mesh.numVertices(),
            mesh.numTriangles(),
            timer.milliseconds());

		gpuMesh(mesh);
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
        //heightmapRenderer.uniformGrid(Rect::withSize(areaStart, areaSize), -(1 << triangulationDensity));
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
            ImGui::SliderInt("Density", &triangulationDensity, 1, 11);
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
    }
};

int main(int argc, const char *argv[])
{
    return Terrain().run();
}
