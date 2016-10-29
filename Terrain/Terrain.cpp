#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"
#include "Xor/FPSCamera.hpp"
#include "Xor/Blit.hpp"
#include "Xor/Mesh.hpp"
#include "Xor/ProcessingMesh.hpp"

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

struct HeightmapMesh
{
	struct Edge
	{
		// Smaller vertex index is always x
		int2 verts;

		Edge() = default;
		Edge(int2 vs) : verts(vs)
		{
            if (verts.y < verts.x)
                std::swap(verts.x, verts.y);
		}
		Edge(int a, int b) : Edge(int2(a, b)) {}

		bool operator==(const Edge &e) const { return all(verts == e.verts); }
	};

	struct Triangle
	{
		float2 maxErrorPos;
		float  maxError = 0;
		int index = -1;
		// Indices of the 3 vertices
		int3 verts = -1;
		// For each edge of the triangle, the pointer to the opposite
		// triangle or null if none.
		int3 opposite;

		Triangle(int3 verts, int index)
			: index(index)
			, verts(verts)
		{
			opposite = -1;
		}

		bool valid() const { return verts.x >= 0; }
		void invalidate() { verts = -1; }

		Edge edge(int i) const
		{
			if (i == 2)
				return Edge(verts.x, verts.z);
			else
				return Edge(verts[i], verts[i + 1]);
		}

        int findEdge(Edge e) const
        {
            for (int i = 0; i < 3; ++i)
            {
                if (e == edge(i))
                    return i;
            }

            return -1;
        }

		void linkWith(Triangle *t, int edge)
        {
            if (t)
                linkWith(t->index, edge);
            else
                linkWith(-1, edge);
        }

		void linkWith(int t, int edge)
		{
			opposite[edge] = t;

			if (t < 0)
				return;

			auto e = this->edge(edge);
			auto tri = other(t);

            int o = tri->findEdge(e);
            if (o >= 0)
                tri->opposite[o] = index;
            else
                XOR_CHECK(false, "Triangles are not adjacent");

            XOR_ASSERT(!all(sortVector(verts) == sortVector(tri->verts)), "Identical triangles");
		}

        void severLink(int edge)
        {
            auto tri = opp(edge);
            if (!tri)
                return;

            int o = tri->findEdge(this->edge(edge));
            if (o >= 0)
                tri->opposite[o] = -1;
        }

        void remove()
        {
            severLink(0);
            severLink(1);
            severLink(2);
        }

		Triangle *other(int triangle)
		{
			if (triangle < 0)
				return nullptr;

			int diff = triangle - index;
			return this + diff;
		}

		Triangle *opp(int i)
		{
			return other(O(i));
		}

		Triangle *opp(Edge e)
        {
            int i = findEdge(e);
            if (i < 0)
                return nullptr;
            else
                return opp(i);
        }

        int V(int i) const
        {
            return verts[i % 3];
        }

        int O(int i) const
        {
            return opposite[i % 3];
        }
	};

	int insertVertex(float3 v)
	{
		int i = static_cast<int>(vertices.size());
		vertices.emplace_back(v);
		return i;
	}

	int insertTriangle(int3 verts)
	{
		int i = static_cast<int>(triangles.size());
        triangles.reserve(10000);
		triangles.emplace_back(verts, i);
		return i;
	}

	float3 V(int i) const
	{
		return vertices[i];
	}

	Triangle &T(int i)
	{
		return triangles[i];
	}

	void initWithCorners()
	{
		vertices.clear();
		triangles.clear();

		vertices.emplace_back(0.f, 0.f, 0.f);
		vertices.emplace_back(1.f, 0.f, 0.f);
		vertices.emplace_back(0.f, 0.f, 1.f);
		vertices.emplace_back(1.f, 0.f, 1.f);

		int t0 = insertTriangle({0, 1, 2});
		int t1 = insertTriangle({1, 2, 3});

		T(t0).linkWith(t1, 1);
	}

	void triangulate(int containingTriangle, float3 newVertex)
	{
		int n = insertVertex(newVertex);

		int3 v0 = T(containingTriangle).verts;
		int3 v1 = v0.s_yz0;
		int3 v2 = v0.s_xz0;

		v0.z = n;
		v1.z = n;
		v2.z = n;

		int t0 = insertTriangle(v0);
		int t1 = insertTriangle(v1);
		int t2 = insertTriangle(v2);

		T(t0).linkWith(t1, 1);
		T(t0).linkWith(t2, 2);
		T(t1).linkWith(t2, 1);

		auto &c = T(containingTriangle);
		T(t0).linkWith(c.opposite[0], 0);
		T(t1).linkWith(c.opposite[1], 0);
		T(t2).linkWith(c.opposite[2], 0);

		c.invalidate();

		{
            flippedTriangles.clear();

			int ts[] = { t0, t1, t2 };
			for (int i = 0; i < 3; ++i)
			{
                static const float PFlip = .5f;
				if (std::uniform_real_distribution<float>()(gen) < PFlip)
					flipEdge(2, ts[i]);
			}
		}
	}

	void flipEdge(int vAIndex, int tABC)
	{
        if (flippedTriangles.count(tABC))
            return;

		auto abc = &T(tABC);
        int vA = abc->V(vAIndex);
		int vB = abc->V(vAIndex + 1);
		int vC = abc->V(vAIndex + 2);

        auto bcd = abc->opp({vB, vC});

		if (!bcd || flippedTriangles.count(bcd->index))
			return;

        auto tAB = abc->opp({vA, vB});
        auto tAC = abc->opp({vA, vC});

		int vD = -1;
        if (bcd->verts.x == vB)
        {
            if (bcd->verts.y == vC)
                vD  = bcd->verts.z;
            else
                vD  = bcd->verts.y;
        }
        else if (bcd->verts.x == vC)
        {
            if (bcd->verts.y == vB)
                vD  = bcd->verts.z;
            else
                vD  = bcd->verts.y;
        }
        else
        {
            if (bcd->verts.y == vB)
                vD  = bcd->verts.x;
            else
                vD  = bcd->verts.x;
        }

        // Test that the quad ABCD is convex, or it can't be flipped
        {
            float2 pA = V(vA).s_xz;
            float2 pB = V(vB).s_xz;
            float2 pC = V(vC).s_xz;
            float2 pD = V(vD).s_xz;

            // It is convex iff the vertex D lies on different sides
            // of the directed edges AB and AC
            float ABD = orient2D(pA, pB, pD);
            float ACD = orient2D(pA, pC, pD);

            // It is on different sides if the sign of the product is negative
            if (ABD * ACD >= 0)
                return;
        }

#if 0
        XOR_ASSERT(vA != vB, "Halp");
        XOR_ASSERT(vA != vC, "Halp");
        XOR_ASSERT(vA != vD, "Halp");
        XOR_ASSERT(vB != vC, "Halp");
        XOR_ASSERT(vB != vD, "Halp");
        XOR_ASSERT(vC != vD, "Halp");
#endif

        XOR_ASSERT(vD >= 0, "Triangle BCD broke assumptions");

		// ABC becomes ABD
		// BCD becomes ACD

        auto tBD = bcd->opp({vB, vD});
        auto tCD = bcd->opp({vC, vD});

#if 0
        auto tAB = oAB < 0 ? nullptr : &T(oAB);
        auto tBD = oBD < 0 ? nullptr : &T(oBD);
        auto tAC = oAC < 0 ? nullptr : &T(oAC);
        auto tCD = oCD < 0 ? nullptr : &T(oCD);
#endif

#if 0
        XOR_ASSERT(oAB < 0 || oAB != oAC, "Halp");
        XOR_ASSERT(oAB < 0 || oAB != oBD, "Halp");
        XOR_ASSERT(oAB < 0 || oAB != oCD, "Halp");
        XOR_ASSERT(oAC < 0 || oAC != oBD, "Halp");
        XOR_ASSERT(oAC < 0 || oAC != oCD, "Halp");
        XOR_ASSERT(oBD < 0 || oBD != oCD, "Halp");
#endif

#if 0
        XOR_CHECK(toAB.findEdge({vA, vB}) >= 0, "AB");
        XOR_CHECK(toBD.findEdge({vB, vD}) >= 0, "BD");
        XOR_CHECK(toAC.findEdge({vA, vC}) >= 0, "AC");
        XOR_CHECK(toCD.findEdge({vC, vD}) >= 0, "CD");
#endif

        abc->remove();
        bcd->remove();

        abc->verts = int3(vA, vB, vD);
        bcd->verts = int3(vA, vC, vD);

        abc->linkWith(tAB,        0);
        abc->linkWith(tBD,        1);
        abc->linkWith(bcd->index, 2);
        bcd->linkWith(tAC,        0);
        bcd->linkWith(tCD,        1);
        bcd->linkWith(abc->index, 2);

        flippedTriangles.insert(abc->index);
        flippedTriangles.insert(bcd->index);
	}

	std::vector<float3> vertices;
	std::vector<Triangle> triangles;
	std::mt19937 gen;
    std::unordered_set<uint> flippedTriangles;
};

struct HeightmapRenderer
{
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

#if 0
    void uniformGrid(Rect area, int vertexDistance = 0)
    {
        Timer t;

        area.rightBottom = min(area.rightBottom, heightmap->size);
        if (all(area.size() < uint2(128)))
            area.leftTop = area.rightBottom - 128;

        int2 sz        = int2(area.size());
        float2 szWorld = float2(sz) * heightmap->texelSize;

        HeightmapMesh mesh;

        if (vertexDistance <= 0)
        {
#if defined(_DEBUG)
            static const int DefaultVertexDim = 256;
#else
            static const int DefaultVertexDim = 1024;
#endif
            int vertexDim = (vertexDistance < 0) ? -vertexDistance : DefaultVertexDim;
            int minDim = std::min(sz.x, sz.y);
            vertexDistance = minDim / vertexDim;
        }

        int2 verts = sz / vertexDistance;
        float2 fVerts = float2(verts);
        float2 fRes   = float2(sz);
        minWorld = -szWorld / 2.f;
		maxWorld = minWorld + szWorld;

        auto heightData = heightmap->image.subresource(0);
		auto numVerts   = (verts.x + 1) * (verts.y + 1);

        mesh.normalizedPos.reserve(numVerts);
        mesh.uv.reserve(numVerts);
        mesh.height.reserve(numVerts);
        mesh.indices.reserve(verts.x * verts.y * (3 * 2));

		float2 invSize = 1.f / float2(heightmap->size);

        for (int y = 0; y <= verts.y; ++y)
        {
            for (int x = 0; x <= verts.x; ++x)
            {
                int2 vertexGridCoords = int2(x, y);
                int2 texCoords = min(vertexGridCoords * vertexDistance + area.leftTop, heightmap->size - 1);
                float2 uv = float2(vertexGridCoords * vertexDistance) / fRes;

                float height = heightData.pixel<float>(uint2(texCoords));

				mesh.normalizedPos.emplace_back(uv);
				mesh.height.emplace_back(height);
                mesh.uv.emplace_back((float2(texCoords) + 0.5f) * invSize);
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

                mesh.indices.emplace_back(ul);
                mesh.indices.emplace_back(dl);
                mesh.indices.emplace_back(ur);
                mesh.indices.emplace_back(dl);
                mesh.indices.emplace_back(dr);
                mesh.indices.emplace_back(ur);
            }
        }

        log("Heightmap", "Generated uniform grid mesh with %zu vertices and %zu indices in %.2f ms\n",
            mesh.normalizedPos.size(),
            mesh.indices.size(),
            t.milliseconds());

		gpuMesh(mesh);
    }
#endif

	void gpuMesh(const HeightmapMesh &mesh)
	{
		auto numVerts = mesh.vertices.size();
		std::vector<float2> normalizedPos(numVerts);
		std::vector<float>  height(numVerts);
		std::vector<float2> uv(numVerts);

		for (uint i = 0; i < numVerts; ++i)
		{
			auto v           = mesh.vertices[i];
			normalizedPos[i] = v.s_xz;
			height[i]        = v.y;
			uv[i]            = normalizedPos[i];
		}

		std::vector<uint> indices;
		indices.reserve(mesh.triangles.size() * 3);
		for (auto &t : mesh.triangles)
		{
			if (!t.valid())
				continue;

			// Negate CCW test because the positions are in UV coordinates,
			// which is left handed because +Y goes down
			bool ccw = !isTriangleCCW(float2(mesh.V(t.verts.x).s_xz),
									  float2(mesh.V(t.verts.y).s_xz),
									  float2(mesh.V(t.verts.z).s_xz));

			if (ccw)
			{
				indices.emplace_back(t.verts.x);
				indices.emplace_back(t.verts.y);
				indices.emplace_back(t.verts.z);
			}
			else
			{
				indices.emplace_back(t.verts.x);
				indices.emplace_back(t.verts.z);
				indices.emplace_back(t.verts.y);
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

		HeightmapMesh mesh;
		mesh.initWithCorners();

		std::mt19937 gen(12345);

		uint t = 0;

		while (mesh.vertices.size() < vertices)
		{
			auto &tri   = mesh.T(t);
			float3 bary = uniformBarycentric(gen);
			float3 v    = bary.x * mesh.V(tri.verts.x)
				        + bary.y * mesh.V(tri.verts.y)
				        + bary.z * mesh.V(tri.verts.z);
			mesh.triangulate(t, v);
			++t;

			XOR_ASSERT(t < mesh.triangles.size(), "Invalid triangle offset");
		}

        log("Heightmap", "Generated random triangulation with %zu vertices and %zu triangles in %.2f ms\n",
            mesh.vertices.size(),
            mesh.triangles.size(),
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
