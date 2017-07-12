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
#include <boost/iterator/counting_iterator.hpp>
#include <algorithm>
#include <fstream>
#include <test/csf/UNL.h>

namespace ripple {
namespace test {
namespace csf {

std::vector<TrustGraph::ForkInfo>
TrustGraph::forkablePairs(double quorum) const
{
    // Check the forking condition by looking at intersection
    // between all pairs of UNLs.

    // First check if some nodes uses a UNL they are not members of, since
    // this creates an implicit UNL with that ndoe.

    auto uniqueUNLs = UNLs_;

    for (int i = 0; i < assignment_.size(); ++i)
    {
        auto const& myUNL = UNLs_[assignment_[i]];
        if (myUNL.find(i) == myUNL.end())
        {
            auto myUNLcopy = myUNL;
            myUNLcopy.insert(i);
            uniqueUNLs.push_back(std::move(myUNLcopy));
        }
    }

    std::vector<ForkInfo> res;
    // Loop over all pairs of uniqueUNLs
    for (int i = 0; i < uniqueUNLs.size(); ++i)
    {
        for (int j = (i + 1); j < uniqueUNLs.size(); ++j)
        {
            auto const& unlA = uniqueUNLs[i];
            auto const& unlB = uniqueUNLs[j];

            double rhs =
                2.0 * (1. - quorum) * std::max(unlA.size(), unlB.size());

            int intersectionSize =
                std::count_if(unlA.begin(), unlA.end(), [&](std::uint32_t id) {
                    return unlB.find(id) != unlB.end();
                });

            if (intersectionSize < rhs)
            {
                res.emplace_back(ForkInfo{i, j, intersectionSize, rhs});
            }
        }
    }
    return res;
}
bool
TrustGraph::canFork(double quorum) const
{
    return !forkablePairs(quorum).empty();
}

TrustGraph
TrustGraph::makeClique(int size, int overlap)
{
    using bci = boost::counting_iterator<std::uint32_t>;

    // Split network into two cliques with the given overlap
    // Clique A has nodes [0,endA) and Clique B has [startB,numPeers)
    // Note: Clique B will have an extra peer when numPeers - overlap
    //       is odd
    int endA = (size + overlap) / 2;
    int startB = (size - overlap) / 2;

    std::vector<UNL> unls;
    unls.emplace_back(bci(0), bci(endA));
    unls.emplace_back(bci(startB), bci(size));
    unls.emplace_back(bci(0), bci(size));

    std::vector<int> assignment(size, 0);

    for (int i = 0; i < size; ++i)
    {
        if (i < startB)
            assignment[i] = 0;
        else if (i >= endA)
            assignment[i] = 1;
        else
            assignment[i] = 2;
    }

    return TrustGraph(unls, assignment);
}

TrustGraph
TrustGraph::makeComplete(int size)
{
    UNL all{boost::counting_iterator<std::uint32_t>(0),
            boost::counting_iterator<std::uint32_t>(size)};

    return TrustGraph(std::vector<UNL>(1, all), std::vector<int>(size, 0));
}

TrustGraph
TrustGraph::makeRingDirected(int size, int neighbors)
{
    std::vector<UNL> UNLs(size);
    std::vector<int> assignments(size);

    if(neighbors > size - 1)
        neighbors = size - 1; // graph will be same as makeComplete(size)

    // UNL i contains nodes i, i+1, ... i+neighbors
    for(uint32_t id = 0; id < size; id++)
    {
        for(uint32_t i = 0; i < neighbors; i++)
        {
            uint32_t peer_id = (id + i) % size;
            UNLs[id].insert(peer_id);
        }
    }

    // each node has its own UNL
    std::iota(assignments.begin(), assignments.end(), 0);
    return TrustGraph(UNLs, assignments);
}

void
TrustGraph::save_dot(std::string const& fileName) const
{
    std::ofstream out(fileName);
    out << "digraph {\n";
    for (int i = 0; i < assignment_.size(); ++i)
    {
        for (auto& j : UNLs_[assignment_[i]])
        {
            out << i << " -> " << j << ";\n";
        }
    }
    out << "}\n";
}

}  // csf
}  // test
}  // ripple
