#pragma once

#include "Core/Core.hpp"

// A directed edge data structure for mesh processing:
// https://www.graphics.rwth-aachen.de/media/papers/directed.pdf

#include <vector>

namespace xor
{
    struct EdgeSmall
    {
        int target   = -1; // target vertex
        int opposite = -1; // opposite directed edge

        EdgeSmall() = default;
        EdgeSmall(int start, int target)
            : target(target)
        {}
    };

    struct EdgeMedium : EdgeSmall
    {
        int prev = -1; // previous directed edge in triangle

        EdgeMedium() = default;
        EdgeMedium(int start, int target)
            : EdgeSmall(start, target)
        {}
    };

    struct EdgeFull : EdgeMedium
    {
        int start = -1; // starting vertex
        int next  = -1; // next directed edge in triangle

        EdgeFull() = default;
        EdgeFull(int start, int target)
            : EdgeMedium(start, target)
            , start(start)
        {}
    };

    template <
        typename TriangleData = Empty,
        typename VertexData   = Empty,
        typename EdgeData     = Empty,
        typename EdgeType     = EdgeMedium
    >
    class DirectedEdge
    {
    public:
        struct Triangle : TriangleData
        {
        };

        struct Vertex : VertexData
        {
            float3 pos;    // position of the vertex
            int edge = -1; // an arbitrary directed edge starting from the vertex

            Vertex() = default;
            Vertex(float3 pos) : pos(pos) {}
        };

        struct Edge : EdgeType, EdgeData
        {
            Edge() = default;
            Edge(int start, int target) : EdgeType(start, target) {}
        };

        static_assert(sizeof(Edge) == sizeof(EdgeType) +
                      std::is_empty<EdgeData>::value ? 0 : sizeof(EdgeData),
                      "Empty base optimization for Edge has failed");

        // T, V and E can be used to go between integer indices
        // and references
        Triangle       &T(int t)                   { return m_triangles[t]; }
        const Triangle &T(int t)             const { return m_triangles[t]; }
        int             T(const Triangle &t) const { return static_cast<int>(&t - m_triangles.data()); }
        Vertex       &V(int v)                 { return m_vertices[v]; }
        const Vertex &V(int v)           const { return m_vertices[v]; }
        int           V(const Vertex &v) const { return static_cast<int>(&v - m_vertices.data()); }
        Edge       &E(int e)               { return m_edges[e]; }
        const Edge &E(int e)         const { return m_edges[e]; }
        int         E(const Edge &e) const { return static_cast<int>(&e - m_edges.data()); }

        // The edges of a triangle are stored in indices 3t, 3t+1, 3t+2
        int triangleEdge(int t) const
        {
            return t * 3;
        }
        int3 triangleAllEdges(int t) const
        {
            int e0 = triangleEdge(t);
            return { e0, e0 + 1, e0 + 2 };
        }
        int3 triangleVertices(int t) const
        {
            int3 edges = triangleAllEdges(t);
            return { edgeTarget(edges.x), edgeTarget(edges.y), edgeTarget(edges.z) };
        }

        int edgeStart(int e) const
        {
            return edgeStart(e, E(e));
        }
        int edgeTarget(int e) const
        {
            return E(e).target;
        }
        int edgeOpposite(int e) const
        {
            return E(e).opposite;
        }

        void clear()
        {
            m_vertices.clear();
            m_triangles.clear();
            m_edges.clear();
        }

        int numVertices()  const { return static_cast<int>(m_vertices.size()); }
        int numTriangles() const { return static_cast<int>(m_triangles.size()); }
        int numEdges()     const { return static_cast<int>(m_edges.size()); }

        Span<const Vertex> vertices() const { return m_vertices; }
        // Construct an index buffer for the mesh
        std::vector<int> triangleIndices() const
        {
            std::vector<int> indices;

            int ts = numTriangles();
            indices.reserve(ts * 3);

            for (int t = 0; t < ts; ++t)
            {
                int3 verts = triangleVertices(t);
                indices.emplace_back(verts.x);
                indices.emplace_back(verts.y);
                indices.emplace_back(verts.z);
            }

            return indices;
        }

        int addVertex(float3 pos)
        {
            int v = addData(m_freeVertices, m_vertices);
            V(v) = Vertex(pos);
            return v;
        }

        int addEdge(int start, int target)
        {
            int e = addData(m_freeEdges, m_edges);
            E(e) = Edge(start, target);
            return e;
        }

    private:
        std::vector<int>      m_freeVertices;
        std::vector<int>      m_freeEdges;
        std::vector<int>      m_freeTriangles;

        std::vector<Vertex>   m_vertices;
        std::vector<Triangle> m_triangles;
        std::vector<Edge>     m_edges;

        template <typename T>
        int addData(std::vector<int> &free, std::vector<T> &data)
        {
            int i = -1;

            if (free.empty())
            {
                i = static_cast<int>(free.size());
                data.emplace_back();
            }
            else
            {
                i = free.back();
                free.pop_back();
            }

            return i;
        }

        int edgePrev(int e, const EdgeMedium &m) const
        {
            return f.prev;
        }

        int edgePrev(int e, const EdgeSmall &) const
        {
            return (e % 3 == 0) ? e + 2 : e - 1;
        }

        int edgeStart(int e, const EdgeFull &f) const
        {
            return f.start;
        }

        template <typename NotFull>
        int edgeStart(int e, const NotFull &nf) const
        {
            return edgeTarget(edgePrev(e, nf));
        }
    };
}
