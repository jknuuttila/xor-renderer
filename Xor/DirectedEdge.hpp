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
        int neighbor = -1; // opposite directed edge

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

        // Return the main edge at 3t, which goes from the first vertex to the second.
        int triangleEdge(int t) const
        {
            return t * 3;
        }
        // Return the three edges of the triangle, in order.
        int3 triangleAllEdges(int t) const
        {
            int e0 = triangleEdge(t);
            return { e0, e0 + 1, e0 + 2 };
        }
        // Return the three vertices of the triangles such that
        // the first returned vertex is the start vertex of
        // triangleEdge(t) and the second is its target.
        int3 triangleVertices(int t) const
        {
            int3 edges = triangleAllEdges(t);
            return { edgeTarget(edges.z), edgeTarget(edges.x), edgeTarget(edges.y) };
        }

        int edgeStart(int e) const
        {
            return edgeStart(e, E(e));
        }
        int edgeTarget(int e) const
        {
            return E(e).target;
        }
        int edgeNeighbor(int e) const
        {
            return E(e).neighbor;
        }
        int edgeTriangle(int e) const
        {
            return e / 3;
        }
        bool edgeIsBoundary(int e) const
        {
            return edgeNeighbor(e) < 0;
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

        // Add a new unconnected vertex
        int addVertex(float3 pos)
        {
            int v = addData(m_freeVertices, m_vertices);
            V(v) = Vertex(pos);
            return v;
        }

        // Add a new unconnected triangle
        int addTriangle(int v0, int v1, int v2)
        {
            int t = addData(m_freeTriangles, m_triangles);
            int e = triangleEdge(t);
            auto &e0 = E(e);
            auto &e1 = E(e + 1);
            auto &e2 = E(e + 2);
            e0 = Edge(v0, v1);
            e1 = Edge(v1, v2);
            e2 = Edge(v2, v0);
            edgeUpdateNextPrev(e0, e1, e2);
            return e;
        }

        // Add a new triangle by extending from a boundary edge
        // using one new vertex
        int addTriangleToBoundary(int boundaryEdge, float3 newVertexPos)
        {
            XOR_ASSERT(edgeIsBoundary(boundaryEdge), "Given edge is not a boundary edge");
            int v2 = addVertex(newVertexPos);
            int v0 = edgeStart(boundaryEdge);
            int v1 = edgeTarget(boundaryEdge);

            int t = addTriangle(v0, v1, v2);
            // Connect the triangle to the mesh via the formerly
            // boundary edge.
            edgeUpdateNeighbor(boundaryEdge, triangleEdge(t));
        }

        // Subdivide an existing triangle to three triangles by adding a new vertex
        // inside the triangle.
        int3 triangleSubdivide(int t, float3 newVertexPos)
        {
            int3 v = triangleVertices(t);
            int v3 = addVertex(newVertexPos);

            int3 outerEdges = triangleAllEdges(t);

            // Add three new triangles such that the main edge
            // of each is the neighbor to the outer edge
            int t0 = addTriangle(v.x, v.y, v3);
            int t1 = addTriangle(v.y, v.z, v3);
            int t2 = addTriangle(v.z, v.x, v3);

            int3 e0 = triangleAllEdges(t0);
            int3 e1 = triangleAllEdges(t1);
            int3 e2 = triangleAllEdges(t2);

            // Connect the outer edges to the mesh
            edgeUpdateNeighbor(e0.x, outerEdges.x);
            edgeUpdateNeighbor(e1.x, outerEdges.y);
            edgeUpdateNeighbor(e2.x, outerEdges.z);

            // Connect the inside edges to each other.
            edgeUpdateNeighbor(e0.y, e1.z);
            edgeUpdateNeighbor(e0.z, e2.y);
            edgeUpdateNeighbor(e1.y, e2.z);

            return { t0, t1, t2 };
        }

        // As triangleSubdivide, but the position of the new vertex is expressed
        // in barycentric coordinates of the subdivided triangle.
        int3 triangleSubdivideBarycentric(int t, float3 newVertexBary)
        {
            int3 verts = triangleVertices(t);
            float3 p0 = V(verts.x).pos;
            float3 p1 = V(verts.y).pos;
            float3 p2 = V(verts.z).pos;
            return triangleSubdivide(t,
                                     newVertexBary.x * p0 +
                                     newVertexBary.y * p1 +
                                     newVertexBary.z * p2);
        }

    private:
        std::vector<int>      m_freeVertices;
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

        void edgeUpdateNextPrev(EdgeFull &f, int next, int prev)
        {
            f.prev = prev;
            f.next = next;
        }

        void edgeUpdateNextPrev(EdgeMedium &m, int, int prev)
        {
            f.prev = prev;
        }

        void edgeUpdateNextPrev(EdgeSmall &, int, int)
        {}

        void edgeUpdateNextPrev(int e0, int e1, int e2)
        {
            edgeUpdateNextPrev(E(e0), e1, e2);
            edgeUpdateNextPrev(E(e1), e2, e0);
            edgeUpdateNextPrev(E(e2), e0, e1);
        }

        void edgeUpdateNeighbor(int e0, int e1)
        {
            E(e0).neighbor = e1;
            E(e1).neighbor = e0;
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
