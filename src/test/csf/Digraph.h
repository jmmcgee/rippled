//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_TEST_CSF_DIGRAPH_H_INCLUDED
#define RIPPLE_TEST_CSF_DIGRAPH_H_INCLUDED

#include <boost/container/flat_map.hpp>
#include <boost/optional.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/iterator_range.hpp>
#include <fstream>
#include <unordered_map>

namespace ripple {
namespace test {
namespace csf {

struct NoEdgeData
{
};

/** Directed graph

Basic directed graph using adjacency list to represent out edges.

Instances of Vertex uniquely identify vertices in the graph. Instances of
EdgeData is data associated with a connection between two vertices.

Both Vertex and EdgeData should be light-weight and cheap-to-copy.

*/
template <class Vertex, class EdgeData = NoEdgeData>
class Digraph
{
    using Links = boost::container::flat_map<Vertex, EdgeData>;
    using Graph = boost::container::flat_map<Vertex, Links>;
    Graph graph_;

    // For iterating empty links
    Links empty;

public:
    /** Connect two vertices

        @param from The source vertex
        @param to The target vertex
        @param e The edge data
        @return true if the edge was created

    */
    bool
    connect(Vertex from, Vertex to, EdgeData e)
    {
        return graph_[from].emplace(to, e).second;
    }

    /** Connect two vertices using default constructed edge data

        @param from The source vertex
        @param to The target vertex
        @return true if the edge was created

    */
    bool
    connect(Vertex from, Vertex to)
    {
        return graph_[from].emplace(to, EdgeData{}).second;
    }

    /** Disconnect two vertices

        @param from The source vertex
        @param to The target vertex
        @return true if an edge was removed
        If from is not connected to to, this function does nothing.
    */
    bool
    disconnect(Vertex from, Vertex to)
    {
        auto it = graph_.find(from);
        if (it != graph_.end())
        {
            return it->second.erase(to) > 0;
        }
        return false;
    }

    /** Return edge data between two vertices

        @param from The source vertex
        @param to The target vertex
        @return Edge if from has an out edge to to, otherwise nullptr

        @note The pointer may be invalidated if connect(from, f) or
       disconnect(from,f) is called for any edge f.
    */
    boost::optional<EdgeData>
    edge(Vertex from, Vertex to) const
    {
        auto it = graph_.find(from);
        if (it != graph_.end())
        {
            auto edgeIt = it->second.find(to);
            if (edgeIt != it->second.end())
                return edgeIt->second;
        }
        return boost::none;
    }

    /** Check if two vertices are connected

        @param from The source vertex
        @param to The target vertex
        @return true if the from has an out edge to to
    */
    bool
    connected(Vertex from, Vertex to) const
    {
        return edge(from, to) != boost::none;
    }

    /** Range of vertices that have out edges in the graph */
    auto
    outVertices() const
    {
        return boost::adaptors::transform(
            graph_,
            [](typename Graph::value_type const& v) { return v.first; });
    }

    /** Range of vertices that are connected via out edges from from
     */
    auto
    outVertices(Vertex from) const
    {
        auto transform = [](typename Links::value_type const& link) {
            return link.first;
        };
        auto it = graph_.find(from);
        if (it != graph_.end())
            return boost::adaptors::transform(it->second, transform);

        return boost::adaptors::transform(empty, transform);
    }

    // Encapsulates all information about an edge in the graph
    struct Edge
    {
        Vertex from;
        Vertex to;
        EdgeData data;
    };

    /** Return range of out edges

        @param from The source vertex
        @return Range of Edges allowing iteration over the set of out-edge
    */
    auto
    outEdges(Vertex from) const
    {
        auto transform = [from](typename Links::value_type const& link) {
            return Edge{from, link.first, link.second};
        };

        auto it = graph_.find(from);
        if (it != graph_.end())
            return boost::adaptors::transform(it->second, transform);

        return boost::adaptors::transform(empty, transform);
    }

    /** Return out-degree of vertex

        @param from The vertex
        @return The number of outgoing edges/neighbors
    */
    std::size_t
    outDegree(Vertex from) const
    {
        auto it = graph_.find(from);
        if (it != graph_.end())
            return it->second.size();
        return 0;
    }

    /** Save GraphViz dot file

        Save a GraphViz dot description of the graph
        @param fileName The output file (creates)
        @param vertexName A invokable T vertexName(Vertex const &) that
                          returns the name to use for the vertex in the file
                          T must be be ostream-able
    */
    template <class VertexName>
    void
    saveDot(std::string const& fileName, VertexName&& vertexName) const
    {
        std::ofstream out(fileName);
        out << "digraph {\n";
        for (auto const& vData : graph_)
        {
            auto const fromName = vertexName(vData.first);
            for (auto const& eData : vData.second)
            {
                auto const toName = vertexName(eData.first);
                out << fromName << " -> " << toName << ";\n";
            }
        }
        out << "}\n";
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple
#endif
