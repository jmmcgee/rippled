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

#include <test/csf/analysis.h>
#include <test/csf/Sim.h>
#include <test/csf/UNL.h>

namespace ripple {
namespace test {
namespace csf {

void
StaticAnalysis::preFork(Sim const & sim)
{
    TrustGraph const & tg = sim.tg_;
    double const & quorum = sim.parms_.minCONSENSUS_PCT / 100.;
    for (TrustGraph::ForkInfo const& fi : tg.forkablePairs(quorum))
    {
        std::cout << "Can fork N" << fi.nodeA << " " << tg.unl(fi.nodeA)
                  << " N" << fi.nodeB << " " << tg.unl(fi.nodeB)
                  << " overlap " << fi.overlap << " required "
                  << fi.required << "\n";
    };
}

void
StaticAnalysis::postFork(Sim const & sim)
{
    std::cout << "Num Forks: " << sim.forks() << "\n";
    std::cout << "Fully synchronized: " << std::boolalpha
              << sim.synchronized() << "\n";
}
}

} // csf
} //ripple

