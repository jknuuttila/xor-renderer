#pragma once

#include "Core/Core.hpp"

// A directed edge data structure for mesh processing:
// https://www.graphics.rwth-aachen.de/media/papers/directed.pdf

#include <vector>
#include <unordered_set>
#include <unordered_map>

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

#define XOR_DE_DEBUG_EDGE(e, ...)     debugEdge(__FILE__, __LINE__, #e, e, ## __VA_ARGS__)
#define XOR_DE_DEBUG_VERTEX(v, ...)   debugVertex(__FILE__, __LINE__, #v, v, ## __VA_ARGS__)
#define XOR_DE_DEBUG_TRIANGLE(t, ...) debugTriangle(__FILE__, __LINE__, #t, t, ## __VA_ARGS__)

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

        // Return true if the given triangle is a valid triangle in the mesh.
        bool triangleIsValid(int t) const
        {
            if (t < 0 || t >= numTriangles())
                return false;
            else
                return edgeTarget(triangleEdge(t)) >= 0;
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

        int edgePrev(int e) const
        {
            return edgePrev(e, E(e));
        }

        int edgeNext(int e) const
        {
            return edgeNext(e, E(e));
        }

        int edgeTriangle(int e) const
        {
            return static_cast<uint>(e) / 3u;
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
                if (!triangleIsValid(t))
                    continue;

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
            resizeEdges(t);

            int e = triangleEdge(t);
            addEdge(e,     v0, v1);
            addEdge(e + 1, v1, v2);
            addEdge(e + 2, v2, v0);
            edgeUpdateNextPrev(e, e + 1, e + 2);
            return t;
        }

        // Add a new unconnected triangle with new vertices
        int addTriangle(float3 p0, float3 p1, float3 p2)
        {
            return addTriangle(addVertex(p0), addVertex(p1), addVertex(p2));
        }

        // Disconnect the given triangle from the mesh
        void disconnectTriangle(int t)
        {
            int3 es = triangleAllEdges(t);
            disconnectEdge(es.x);
            disconnectEdge(es.y);
            disconnectEdge(es.z);
        }

        // Disconnects an edge from its neighbor and start vertex.
        // Does not affect other edges in the triangle, because
        // their connectivity is fixed.
        void disconnectEdge(int e)
        {
            auto &v = V(edgeStart(e));
            int n = edgeNeighbor(e);

            if (v.edge == e)
                v.edge = -1;

            if (n >= 0)
            {
                if (E(n).neighbor == e)
                    E(n).neighbor = -1;
            }
        }

        // Remove the triangle from the mesh and free its storage.
        // Does not disconnect the triangle, so that must be done separately,
        // either as part of some other algorithm or using disconnectTriangle().
        void removeTriangle(int t)
        {
            int3 es = triangleAllEdges(t);

            XOR_ASSERT(edgeNeighbor(es.x) < 0 || E(edgeNeighbor(es.x)).neighbor != es.x, "Triangle must be disconnected to be removed");
            XOR_ASSERT(edgeNeighbor(es.y) < 0 || E(edgeNeighbor(es.y)).neighbor != es.y, "Triangle must be disconnected to be removed");
            XOR_ASSERT(edgeNeighbor(es.z) < 0 || E(edgeNeighbor(es.z)).neighbor != es.z, "Triangle must be disconnected to be removed");
            XOR_ASSERT(V(edgeStart(es.x)).edge != es.x, "Triangle must be disconnected to be removed");
            XOR_ASSERT(V(edgeStart(es.y)).edge != es.y, "Triangle must be disconnected to be removed");
            XOR_ASSERT(V(edgeStart(es.z)).edge != es.z, "Triangle must be disconnected to be removed");

            E(es.x) = Edge();
            E(es.y) = Edge();
            E(es.z) = Edge();
            removeData(t, m_freeTriangles, m_triangles);
        }

        // Add a new triangle by extending from a boundary edge
        // using one new vertex
        int addTriangleToBoundary(int boundaryEdge, float3 newVertexPos)
        {
            XOR_ASSERT(edgeIsBoundary(boundaryEdge), "Given edge is not a boundary edge");
            int v2 = addVertex(newVertexPos);
            int v0 = edgeStart(boundaryEdge);
            int v1 = edgeTarget(boundaryEdge);

            // The new triangle is on the other side of the boundary edge, so
            // its corresponding edge must go in the other direction, from 1 to 0.
            int t = addTriangle(v1, v0, v2);
            // Connect the triangle to the mesh via the formerly
            // boundary edge.
            edgeUpdateNeighbor(boundaryEdge, triangleEdge(t));

            return t;
        }

        // Subdivide an existing triangle to three triangles by adding a new vertex
        // inside the triangle.
        int3 triangleSubdivide(int t, float3 newVertexPos)
        {
            int v = addVertex(newVertexPos);

            int3 outerEdges = triangleAllEdges(t);

            // Add three new triangles such that the main edge
            // of each is the neighbor to the outer edge
            int t0 = addTriangle(edgeStart(outerEdges.x), edgeTarget(outerEdges.x), v);
            int t1 = addTriangle(edgeStart(outerEdges.y), edgeTarget(outerEdges.y), v);
            int t2 = addTriangle(edgeStart(outerEdges.z), edgeTarget(outerEdges.z), v);

            int3 e0 = triangleAllEdges(t0);
            int3 e1 = triangleAllEdges(t1);
            int3 e2 = triangleAllEdges(t2);

            // Connect the outer edges to the mesh
            edgeUpdateNeighbor(e0.x, edgeNeighbor(outerEdges.x));
            edgeUpdateNeighbor(e1.x, edgeNeighbor(outerEdges.y));
            edgeUpdateNeighbor(e2.x, edgeNeighbor(outerEdges.z));

            // Connect the inside edges to each other.
            edgeUpdateNeighbor(e0.y, e1.z);
            edgeUpdateNeighbor(e0.z, e2.y);
            edgeUpdateNeighbor(e1.y, e2.z);

            // Remove the old triangle
            removeTriangle(t);

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

        // Return true if the edge can be flipped using edgeFlip, when the triangles are
        // interpreted as 2D triangles using their XY coordinates.
        bool edgeIsFlippable(int e) const
        {
            int n = edgeNeighbor(e);
            if (n < 0)
                return false;

            int A = edgeTarget(edgeNext(e));
            int B = edgeTarget(n);
            int C = edgeTarget(e);
            int D = edgeTarget(edgeNext(n));

            return isQuadConvex(
                float2(V(A).pos),
                float2(V(B).pos),
                float2(V(C).pos),
                float2(V(D).pos));
        }

        // Given an edge BC that is a diagonal of the convex quadrilateral ABDC formed
        // by the triangles ABC and DCB, flip the edge, replacing ABC with ABD and DCB
        // with DCA. Return the edge that is the new diagonal DA belonging to ABD.
        int edgeFlip(int e)
        {
            // First, dig up all the related edges and vertices.
            int eBC = e;
            int eAB = edgePrev(eBC);
            int eCA = edgePrev(eAB);

            int eCB = edgeNeighbor(eBC);
            XOR_ASSERT(eCB >= 0, "Flipped edge has no neighbor, meaning it's not in a quadrilateral");
            int eDC = edgePrev(eCB);
            int eBD = edgePrev(eDC);

            int vA = edgeTarget(eCA);
            int vB = edgeTarget(eCB);
            int vC = edgeTarget(eBC);
            int vD = edgeTarget(eBD);

            int nAB = edgeNeighbor(eAB);
            int nCA = edgeNeighbor(eCA);
            int nDC = edgeNeighbor(eDC);
            int nBD = edgeNeighbor(eBD);

            // DA and AD are completely new edges, and prev edges
            // with respect to the intact edges (AB and DC) in the
            // triangles. Use the previous prev edges of the intact edges
            // for them.
            int eDA = eCA;
            int eAD = eBD;

            // CA and BD both already exist, but will transfer from one
            // triangle to another. Use the edges from the flipped edge
            // for them, which are also the next edges of the intact edges.
            eCA = eCB;
            eBD = eBC;

            // Now we have established locations and proper names for
            // the new edges. Now fix up the data.
            addEdge(eBD, vB, vD);
            addEdge(eCA, vC, vA);
            addEdge(eDA, vD, vA);
            addEdge(eAD, vA, vD);

            // Update edge connectivity to match the new triangles.
            edgeUpdateNextPrev(eAB, eBD, eDA);
            edgeUpdateNextPrev(eDC, eCA, eAD);

            // Connect the new triangles to external neighbors
            edgeUpdateNeighbor(eAB, nAB);
            edgeUpdateNeighbor(eCA, nCA);
            edgeUpdateNeighbor(eDC, nDC);
            edgeUpdateNeighbor(eBD, nBD);

            // And finally, to each other
            edgeUpdateNeighbor(eDA, eAD);

            return eDA;
        }

        void edgeUpdateNextPrev(int e0, int e1, int e2)
        {
            XOR_ASSERT(e0 + 1 == e1 || e0 - 2 == e1, "Edge connectivity must match to edge numbers");
            XOR_ASSERT(e1 + 1 == e2 || e1 - 2 == e2, "Edge connectivity must match to edge numbers");
            XOR_ASSERT(e2 + 1 == e0 || e2 - 2 == e0, "Edge connectivity must match to edge numbers");

            edgeUpdateNextPrev(E(e0), e1, e2);
            edgeUpdateNextPrev(E(e1), e2, e0);
            edgeUpdateNextPrev(E(e2), e0, e1);
        }

        void edgeUpdateNeighbor(int e0, int e1)
        {
            XOR_ASSERT((e0 < 0 || e1 < 0) || edgeTarget(e0) == edgeStart(e1), "Neighboring edges must have the same vertices in opposite order");
            XOR_ASSERT((e0 < 0 || e1 < 0) || edgeTarget(e1) == edgeStart(e0), "Neighboring edges must have the same vertices in opposite order");

            if (e0 >= 0) E(e0).neighbor = e1;
            if (e1 >= 0) E(e1).neighbor = e0;
        }

        void debugEdge(const char *file, int line, const char *name, int e, const char *prefix = nullptr) const
        {
            if (e >= 0)
                print("%s%sEdge \"%s\" (%d): (%d -> %d) neighbor: %d\n",
                      prefix ? prefix : "", prefix ? " " : "",
                      name, e, edgeStart(e), edgeTarget(e), edgeNeighbor(e));
            else
                print("%s%sEdge \"%s\" (%d)\n", file, line, name, e);
        }

        void debugVertex(const char *file, int line, const char *name, int v, const char *prefix = nullptr) const
        {
            print("%s%sVertex \"%s\" (%d): (%.3f %.3f %.3f) edge: %d\n",
                  prefix ? prefix : "", prefix ? " " : "",
                  name, v,
                  V(v).pos.x,
                  V(v).pos.y,
                  V(v).pos.z,
                  V(v).edge);
        }

        void debugTriangle(const char *file, int line, const char *name, int t, const char *prefix = nullptr) const
        {
            auto vs = triangleVertices(t);
            print("%s%sTriangle \"%s\" (%d): (%d %d %d)\n",
                  prefix ? prefix : "", prefix ? " " : "",
                  name, t, vs.x, vs.y, vs.z);
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
                i = static_cast<int>(data.size());
                data.emplace_back();
            }
            else
            {
                i = free.back();
                free.pop_back();
            }

            return i;
        }

        void resizeEdges(int t)
        {
            size_t minEdges = (t + 1) * 3;
            if (m_edges.size() < minEdges)
                m_edges.resize(minEdges);
        }

        template <typename T>
        void removeData(int i, std::vector<int> &free, std::vector<T> &data)
        {
            data[i] = T();
            free.emplace_back(i);
        }

        Edge &addEdge(int e, int start, int target)
        {
            auto &edge = E(e);
            edge = Edge(start, target);
            V(start).edge = e;
            return edge;
        }

        void edgeUpdateNextPrev(EdgeFull &f, int next, int prev)
        {
            f.prev = prev;
            f.next = next;
        }

        void edgeUpdateNextPrev(EdgeMedium &m, int next, int prev)
        {
            m.prev = prev;
        }

        void edgeUpdateNextPrev(EdgeSmall &, int, int)
        {}

        int edgePrev(int e, const EdgeMedium &m) const
        {
            return m.prev;
        }

        int edgePrev(int e, const EdgeSmall &) const
        {
            return (static_cast<uint>(e) % 3u == 0u) ? e + 2 : e - 1;
        }

        int edgeNext(int e, const EdgeSmall &) const
        {
            return (static_cast<uint>(e) % 3u == 2u) ? e - 2 : e + 1;
        }

        int edgeNext(int e, const EdgeFull &f) const
        {
            return f.next;
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

    // Helper class to perform Delaunay triangulation using the Bowyer-Watson
    // algorithm on a given DirectedEdge mesh.
    template <typename DE>
    class BowyerWatson
    {
        // Mesh that this algorithm operates on
        DE &mesh;
        // Triangles that have already been checked for circumcircle violations
        // during the current insertion.
        std::unordered_set<int> m_trisExplored;
        // Triangles that will be checked for circumcircle violations.
        std::vector<int> m_trisToExplore;
        std::unordered_set<int> m_removedEdges;
        std::vector<int> m_removedTriangles;
        std::vector<int> m_removedBoundary;
        std::unordered_map<int, int> m_vertexNeighbors;
    public:
        BowyerWatson(DE &mesh) : mesh(mesh) {}

        // Assuming that the triangulation is already a Delaunay triangulation,
        // insert one new vertex inside the specified triangle, and retriangulate
        // so that the mesh stays Delaunay.
        void insertVertex(int containingTriangle,
                          float3 newVertexPos,
                          std::vector<int> *newTriangles = nullptr,
                          std::vector<int> *removedTriangles = nullptr)
        {
            m_removedTriangles.clear();
            m_removedEdges.clear();
            m_trisToExplore.clear();
            m_trisExplored.clear();
            m_trisToExplore.emplace_back(containingTriangle);

            while (!m_trisToExplore.empty())
            {
                int tri = m_trisToExplore.back();
                m_trisToExplore.pop_back();

                bool removeTriangle = m_trisExplored.empty();

                m_trisExplored.insert(tri);

                // The first triangle is the triangle we placed the vertex in,
                // which will be removed by definition. We don't check the
                // circumcircle for it to avoid numerical errors and the situation
                // where we would remove no triangles, which is impossible.
                if (!removeTriangle)
                {
                    int3 verts = mesh.triangleVertices(tri);

                    float2 v0 = float2(mesh.V(verts.x).pos);
                    float2 v1 = float2(mesh.V(verts.y).pos);
                    float2 v2 = float2(mesh.V(verts.z).pos);

                    float2 inside = (v0 + v1 + v2) / 3.f;

                    float insideSign = pointsOnCircle(v0, v1, v2, inside);
                    float posSign    = pointsOnCircle(v0, v1, v2, float2(newVertexPos));

                    // If the signs are the same, the product will be non-negative.
                    // This means that pos is inside the circumcircle.
                    removeTriangle = insideSign * posSign > 0;

                    // Do not remove triangles for which the vertex is extremely close
                    // to being on the circumcircle. This usually happens because of
                    // small triangles and numerical instability, and leads to problems
                    // with the removed area not being well-behaved anymore.
                    constexpr float Epsilon = 1e-6f;
                    if (abs(posSign) < Epsilon)
                        removeTriangle = false;
                }

                // Collect the removed triangle and its edges for later processing
                if (removeTriangle)
                {
                    int3 edges = mesh.triangleAllEdges(tri);

                    m_removedTriangles.emplace_back(tri);
                    for (int e : edges.span())
                    {
                        m_removedEdges.insert(e);
                        int n = mesh.edgeNeighbor(e);
                        if (n >= 0)
                        {
                            int tn = mesh.edgeTriangle(n);
                            if (!m_trisExplored.count(tn))
                                m_trisToExplore.emplace_back(tn);
                        }
                    }
                }
            }

            int newVertex = mesh.addVertex(newVertexPos);
            m_vertexNeighbors.clear();
            m_removedBoundary.clear();

            XOR_ASSERT(!m_removedEdges.empty(), "Each new vertex should delete at least one triangle");

            // Separate the boundary edges from the inside edges
            for (int e : m_removedEdges)
            {
                int n = mesh.edgeNeighbor(e);
                if (n < 0 || !m_removedEdges.count(n))
                    m_removedBoundary.emplace_back(e);
            }

            // Create new triangles in the removed area, and connect
            // them to the mesh.
            for (int e : m_removedBoundary)
            {
                int3 vs;
                vs.x = mesh.edgeStart(e);
                vs.y = mesh.edgeTarget(e);
                vs.z = newVertex;

                int newTriangle = mesh.addTriangle(vs.x, vs.y, vs.z);
                int3 es = mesh.triangleAllEdges(newTriangle);

                int n = mesh.edgeNeighbor(e);
                mesh.edgeUpdateNeighbor(es.x, n);

                auto updateVertexNeighbors = [&](int vert, int edge)
                {
                    auto it = m_vertexNeighbors.find(vert);
                    if (it == m_vertexNeighbors.end())
                        m_vertexNeighbors.insert(it, { vert, edge });
                    else
                        mesh.edgeUpdateNeighbor(edge, it->second);
                };

                updateVertexNeighbors(vs.y, es.y);
                updateVertexNeighbors(vs.x, es.z);

                if (newTriangles)
                    newTriangles->emplace_back(newTriangle);
            }

            if (removedTriangles)
                removedTriangles->insert(removedTriangles->begin(),
                                         m_removedTriangles.begin(),
                                         m_removedTriangles.end());

            // Finally, actually delete the removed triangles, which should now
            // be unconnected except maybe to other removed triangles.
            for (int tri : m_removedTriangles)
            {
                mesh.disconnectTriangle(tri);
                mesh.removeTriangle(tri);
            }
        }
    };

}
