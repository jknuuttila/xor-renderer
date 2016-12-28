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
        typename TriangleData       = Empty,
        typename VertexPositionType = float3,
        typename VertexData         = Empty,
        typename EdgeData           = Empty,
        typename EdgeType           = EdgeMedium
    >
    class DirectedEdge
    {
    public:
        using VertexPosition = VertexPositionType;

        struct Triangle : TriangleData
        {
        };

        struct Vertex : VertexData
        {
            VertexPosition pos;    // position of the vertex
            int edge = -1; // an arbitrary directed edge starting from the vertex

            Vertex() = default;
            Vertex(VertexPosition pos) : pos(pos) {}
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

        // Return the positions of the three vertices of the triangle.
        std::array<VertexPosition, 3> triangleVertexPositions(int t) const
        {
            std::array<VertexPosition, 3> ps;
            int3 verts = triangleVertices(t);
            ps[0] = V(verts.x).pos;
            ps[1] = V(verts.y).pos;
            ps[2] = V(verts.z).pos;
            return ps;
        }

        // Return true if the given vertex is valid and connected to the mesh.
        bool vertexIsValid(int v) const
        {
            if (v < 0 || v >= numVertices())
                return false;
            else
                return vertexEdge(v) >= 0;
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

        int vertexEdge(int v) const
        {
            return V(v).edge;
        }

        template <typename F>
        int vertexForEachOutgoingEdge(int v, F &&f) const
        {
            int count = 0;
            int firstEdge = vertexEdge(v);
            int e = firstEdge;

            if (e < 0)
                return count;

            for (;;)
            {
                f(e);
                ++count;

                // e always points away from the edge, so its neighbor,
                // if any, points to it.
                int n = edgeNeighbor(e);

                // If we hit an edge with no neighbor, the vertex is
                // a boundary vertex and we need to loop the other
                // direction also.
                if (n < 0)
                    break;

                // Because n points to the edge, its successor points away
                // from it. They both belong to the same triangle, which
                // is a different triangle than the one iterated just before.
                e = edgeNext(n);

                // Once we hit the first edge again, we are done.
                if (e == firstEdge)
                    return count;
            }

            e = firstEdge;

            for (;;)
            {
                // e points away from the edge, so its predecessor points to it.
                int p = edgePrev(e);

                // And its neighbor again points away from it.
                int n = edgeNeighbor(p);

                if (n < 0 || n == firstEdge)
                    return count;

                f(n);
                ++count;
                
                e = n;
            }
        }

        template <typename F>
        int vertexForEachTriangle(int v, F &&f) const
        {
            return vertexForEachOutgoingEdge(v, [&] (int e)
            {
                f(this->edgeTriangle(e));
            });
        }

        template <typename F>
        int vertexForEachAdjacentVertex(int v, F &&f) const
        {
            return vertexForEachOutgoingEdge(v, [&] (int e)
            {
                f(this->edgeTarget(e));
            });
        }

        int vertexRemoveUnconnected()
        {
            int removed = 0;
            for (int v = 0; v < numVertices(); ++v)
            {
                if (!vertexIsValid(v))
                {
                    removeVertex(v);
                    ++removed;
                }
            }

            return removed;
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

        int numValidVertices() const
        {
            int valid = 0;
            for (int v = 0; v < numVertices(); ++v)
                valid += static_cast<int>(vertexIsValid(v));
            return valid;
        }

        int numValidTriangles() const
        {
            int valid = 0;
            for (int t = 0; t < numTriangles(); ++t)
                valid += static_cast<int>(triangleIsValid(t));
            return valid;
        }

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
        int addVertex(VertexPosition pos)
        {
            int v = addData(m_freeVertices, m_vertices);
            V(v) = Vertex(pos);
            return v;
        }

        void removeVertex(int v)
        {
            XOR_ASSERT(!vertexIsValid(v), "Tried to remove a connected vertex");
            V(v) = Vertex();
            removeData(v, m_freeVertices, m_vertices);
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
        int addTriangle(VertexPosition p0, VertexPosition p1, VertexPosition p2)
        {
            int v0 = addVertex(p0);
            int v1 = addVertex(p1);
            int v2 = addVertex(p2);
            return addTriangle(v0, v1, v2);
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
            int vi = edgeStart(e);
            auto &v = V(vi);
            int n = edgeNeighbor(e);

            if (v.edge == e)
            {
                int anotherEdge = -1;
                vertexForEachOutgoingEdge(vi, [&] (int out)
                {
                    if (out != e)
                        anotherEdge = out;
                });
                v.edge = anotherEdge;
            }

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
            T(t)    = Triangle();
            removeData(t, m_freeTriangles, m_triangles);
        }

        // Add a new triangle by extending from a boundary edge
        // using one new vertex
        int addTriangleToBoundary(int boundaryEdge, VertexPosition newVertexPos)
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
        // inside the triangle. The first edge of each new triangle is the outer edge.
        int3 triangleSubdivide(int t, VertexPosition newVertexPos)
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
        int3 triangleSubdivideBarycentric(int t, VertexPosition newVertexBary)
        {
            int3 verts = triangleVertices(t);
            VertexPosition p0 = V(verts.x).pos;
            VertexPosition p1 = V(verts.y).pos;
            VertexPosition p2 = V(verts.z).pos;
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

            auto pA = V(A).pos.vec2();
            auto pB = V(B).pos.vec2();
            auto pC = V(C).pos.vec2();
            auto pD = V(D).pos.vec2();

            return isQuadConvex(pA, pB, pC, pD);
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
            {
                print("%s%sEdge \"%s\" (%d): (%d -> %d) neighbor: %d\n",
                      prefix ? prefix : "", prefix ? " " : "",
                      name, e, edgeStart(e), edgeTarget(e), edgeNeighbor(e));
            }
            else
            {
                print("%s%sEdge \"%s\" (%d)\n",
                      prefix ? prefix : "", prefix ? " " : "",
                      name, e);
            }
        }

        void debugVertex(const char *file, int line, const char *name, int v, const char *prefix = nullptr) const
        {
            if (v >= 0)
            {
                print("%s%sVertex \"%s\" (%d): (%.3f %.3f %.3f) edge: %d\n",
                      prefix ? prefix : "", prefix ? " " : "",
                      name, v,
                      V(v).pos.x,
                      V(v).pos.y,
                      V(v).pos.z,
                      V(v).edge);
            }
            else
            {
                print("%s%sVertex \"%s\" (%d)\n",
                      prefix ? prefix : "", prefix ? " " : "",
                      name, v);
            }
        }

        void debugTriangle(const char *file, int line, const char *name, int t, const char *prefix = nullptr) const
        {
            if (t >= 0)
            {
                auto vs = triangleVertices(t);
                print("%s%sTriangle \"%s\" (%d): (%d %d %d)\n",
                      prefix ? prefix : "", prefix ? " " : "",
                      name, t, vs.x, vs.y, vs.z);
            }
            else
            {
                print("%s%sTriangle \"%s\" (%d)\n",
                      prefix ? prefix : "", prefix ? " " : "",
                      name, t);
            }
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
        // using DE = DirectedEdge<>;
        // Mesh that this algorithm operates on
        DE &mesh;
        using Pos = typename DE::VertexPosition;

        // Triangles that have already been checked for circumcircle violations
        // during the current insertion.
        std::unordered_set<int> m_trisExplored;
        // Triangles that will be checked for circumcircle violations.
        std::vector<int> m_trisToExplore;
        std::unordered_set<int> m_removedEdges;
        std::unordered_set<int> m_removedTriangles;
        std::vector<int> m_removedBoundary;
        std::unordered_map<int, int> m_vertexNeighbors;
        int3 m_superTriangle = int3(-1);
    public:
        BowyerWatson(DE &mesh) : mesh(mesh) {}

        void superTriangle(Pos pointSetMinBound, Pos pointSetMaxBound)
        {
            XOR_ASSERT(all(m_superTriangle == int3(-1)), "Supertriangle can only be set once");
            Pos dims = pointSetMaxBound - pointSetMinBound;
            Pos center = pointSetMinBound + dims / 2;

            auto maxDim = std::max(dims.x, dims.y);
            auto enclosingDim = maxDim * 10;

            auto v0 = Pos(center.x,                center.y - enclosingDim);
            auto v1 = Pos(center.x - enclosingDim, center.y + enclosingDim);
            auto v2 = Pos(center.x + enclosingDim, center.y + enclosingDim);

            int t = mesh.addTriangle(v0, v1, v2);
            m_superTriangle = mesh.triangleVertices(t);
        }

        // Assuming that the triangulation is already a Delaunay triangulation,
        // insert one new vertex inside the specified triangle, and retriangulate
        // so that the mesh stays Delaunay. Returns the new vertex ID.
        int insertVertex(int containingTriangle,
                         Pos newVertexPos,
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

                if (m_removedTriangles.count(tri))
                    continue;

                bool removeTriangle = m_trisExplored.empty();

                m_trisExplored.insert(tri);

                // The first triangle is the triangle we placed the vertex in,
                // which will be removed by definition. We don't check the
                // circumcircle for it to avoid numerical errors and the situation
                // where we would remove no triangles, which is impossible.
                if (!removeTriangle)
                {
                    int3 verts = mesh.triangleVertices(tri);

                    auto v0 = mesh.V(verts.x).pos.vec2();
                    auto v1 = mesh.V(verts.y).pos.vec2();
                    auto v2 = mesh.V(verts.z).pos.vec2();

                    removeTriangle = inCircleUnknownWinding(v0, v1, v2, newVertexPos.vec2());
                }

                // Collect the removed triangle and its edges for later processing
                if (removeTriangle)
                {
                    int3 edges = mesh.triangleAllEdges(tri);

                    m_removedTriangles.insert(tri);
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

            return newVertex;
        }

        // Like insertVertex(t, pos, ...), but searches for the containing triangle instead.
        int insertVertex(Pos newVertexPos,
                         std::vector<int> *newTriangles = nullptr,
                         std::vector<int> *removedTriangles = nullptr)
        {
            for (int t = 0; t < mesh.numTriangles(); ++t)
            {
                if (mesh.triangleIsValid(t))
                {
                    auto vs = mesh.triangleVertexPositions(t);

                    if (isPointInsideTriangleUnknownWinding(
                        vs[0].vec2(),
                        vs[1].vec2(),
                        vs[2].vec2(),
                        newVertexPos.vec2()))
                        return insertVertex(t, newVertexPos, newTriangles, removedTriangles);
                }
            }

            XOR_ASSERT(false, "Could not find a triangle to insert the vertex in");
            return -1;
        }

        void removeSuperTriangle()
        {
            m_removedTriangles.clear();

            for (int v : m_superTriangle.span())
            {
                mesh.vertexForEachTriangle(v, [&](int t)
                {
                    m_removedTriangles.emplace(t);
                });
            }

            for (int t : m_removedTriangles)
            {
                mesh.disconnectTriangle(t);
                mesh.removeTriangle(t);
            }

            m_superTriangle = int3(-1);
        }

        bool triangleContainsSuperVertices(int t) const
        {
            if (m_superTriangle.x < 0)
                return false;

            int3 verts = mesh.triangleVertices(t);
            return any(int3(verts.x) == m_superTriangle) ||
                any(int3(verts.y) == m_superTriangle) ||
                any(int3(verts.z) == m_superTriangle);
        }
    };

    template <typename DE>
    class DelaunayFlip
    {
         // using DE = DirectedEdge<>;

         DE &mesh;
         using Pos = typename DE::VertexPosition;

         std::unordered_set<int> m_affected;
         std::unordered_set<int> m_edges;
         std::unordered_set<int> m_nextEdges;
         int3 m_superTriangle = int3(-1);
    public:

        DelaunayFlip(DE &mesh) : mesh(mesh)
        {}

        int insertVertex(int containingTriangle,
                         Pos newVertexPos,
                         std::vector<int> *affectedTriangles = nullptr)
        {
            m_edges.clear();
            m_affected.clear();

            m_affected.emplace(containingTriangle);

            int3 ts = mesh.triangleSubdivide(containingTriangle, newVertexPos);
            int newVertex = mesh.edgeTarget(mesh.edgeNext(mesh.triangleEdge(ts.x)));

            m_affected.emplace(ts.x);
            m_affected.emplace(ts.y);
            m_affected.emplace(ts.z);

            m_nextEdges.emplace(mesh.triangleEdge(ts.x));
            m_nextEdges.emplace(mesh.triangleEdge(ts.y));
            m_nextEdges.emplace(mesh.triangleEdge(ts.z));

            constexpr int InfiniteLoopGuard = 10;
            constexpr int MaxLoops = 500;
            int loops = 0;
            int totalLoops = 0; 
            std::unordered_set<int> prevEdges;

            while (!m_nextEdges.empty())
            {
                XOR_ASSERT(m_edges.empty(), "Edges unexpectedly empty");
                m_nextEdges.swap(m_edges);

                if (totalLoops++ > MaxLoops)
                    break;

                if (loops++ > InfiniteLoopGuard)
                {
                    if (prevEdges.empty())
                    {
                        prevEdges.insert(m_edges.begin(), m_edges.end());
                    }
                    else if (prevEdges.size() == m_edges.size())
                    {
                        bool inInfiniteLoop = true;

                        for (int e : m_edges)
                        {
                            if (!prevEdges.count(e))
                            {
                                inInfiniteLoop = false;
                                break;
                            }
                        }

                        if (inInfiniteLoop)
                        {
                            break;
                        }
                        else
                        {
                            loops = 0;
                            prevEdges.clear();
                        }
                    }
                    else
                    {
                        prevEdges.clear();
                    }
                }

                while (!m_edges.empty())
                {
                    int e = *m_edges.begin();
                    m_edges.erase(e);

                    if (!isLocallyDelaunay(e))
                    {
                        m_affected.emplace(mesh.edgeTriangle(e));
                        m_affected.emplace(mesh.edgeTriangle(mesh.edgeNeighbor(e)));

                        int diagonal = mesh.edgeFlip(e);
                        int n = mesh.edgeNeighbor(diagonal);

                        m_affected.emplace(mesh.edgeTriangle(diagonal));
                        m_affected.emplace(mesh.edgeTriangle(n));

                        m_nextEdges.emplace(mesh.edgePrev(diagonal));
                        m_nextEdges.emplace(mesh.edgeNext(diagonal));
                        m_nextEdges.emplace(mesh.edgePrev(n));
                        m_nextEdges.emplace(mesh.edgeNext(n));
                    }
                }
            }

            if (affectedTriangles)
                affectedTriangles->insert(affectedTriangles->begin(),
                                          m_affected.begin(),
                                          m_affected.end());

            return newVertex;
        }

        bool isLocallyDelaunay(int e) const
        {
            int n = mesh.edgeNeighbor(e);

            if (n < 0)
            {
                return true;
            }

            if (!mesh.edgeIsFlippable(e))
            {
                return true;
            }

            int t0 = mesh.edgeTriangle(e);
            int t1 = mesh.edgeTriangle(n);

            auto pos0 = mesh.triangleVertexPositions(t0);
            int v1 = mesh.edgeTarget(mesh.edgeNext(n));

            if (inCircleUnknownWinding(pos0[0].vec2(),
                                       pos0[1].vec2(),
                                       pos0[2].vec2(),
                                       mesh.V(v1).pos.vec2()))
            {
                return false;
            }

            auto pos1 = mesh.triangleVertexPositions(t1);
            int v0 = mesh.edgeTarget(mesh.edgeNext(e));
            if (inCircleUnknownWinding(pos1[0].vec2(),
                                       pos1[1].vec2(),
                                       pos1[2].vec2(),
                                       mesh.V(v0).pos.vec2()))
            {
                return false;
            }

            return true;
        }

        int insertVertex(Pos newVertexPos,
                         std::vector<int> *affectedTriangles = nullptr)
        {
            for (int t = 0; t < mesh.numTriangles(); ++t)
            {
                if (mesh.triangleIsValid(t))
                {
                    auto vs = mesh.triangleVertexPositions(t);

                    if (isPointInsideTriangleUnknownWinding(
                        vs[0].vec2(),
                        vs[1].vec2(),
                        vs[2].vec2(),
                        newVertexPos.vec2()))
                        return insertVertex(t, newVertexPos, affectedTriangles);
                }
            }

            XOR_ASSERT(false, "Could not find a triangle to insert the vertex in");
            return -1;
        }

        void superTriangle(Pos pointSetMinBound, Pos pointSetMaxBound)
        {
            XOR_ASSERT(all(m_superTriangle == int3(-1)), "Supertriangle can only be set once");
            Pos dims = pointSetMaxBound - pointSetMinBound;
            Pos center = pointSetMinBound + dims / 2;

            auto maxDim = std::max(dims.x, dims.y);
            auto enclosingDim = maxDim * 10 / 3;

            auto v0 = Pos(center.x,                center.y - enclosingDim);
            auto v1 = Pos(center.x - enclosingDim, center.y + enclosingDim);
            auto v2 = Pos(center.x + enclosingDim, center.y + enclosingDim);

            int t = mesh.addTriangle(v0, v1, v2);
            m_superTriangle = mesh.triangleVertices(t);
        }

        void removeSuperTriangle()
        {
            m_affected.clear();

            for (int v : m_superTriangle.span())
            {
                mesh.vertexForEachTriangle(v, [&](int t)
                {
                    m_affected.emplace(t);
                });
            }

            for (int t : m_affected)
            {
                mesh.disconnectTriangle(t);
                mesh.removeTriangle(t);
            }

            m_superTriangle = int3(-1);
        }

        bool triangleContainsSuperVertices(int t) const
        {
            if (m_superTriangle.x < 0)
                return false;

            int3 verts = mesh.triangleVertices(t);
            return any(int3(verts.x) == m_superTriangle) ||
                any(int3(verts.y) == m_superTriangle) ||
                any(int3(verts.z) == m_superTriangle);
        }
    };

}
