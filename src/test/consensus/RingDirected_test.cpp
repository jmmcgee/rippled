//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc->

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
#include <BeastConfig.h>
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <test/csf.h>
#include <utility>

namespace ripple {
namespace test {

class RingDirected_test : public beast::unit_test::suite
{
public:
    void
    testPeersAgree()
    {
        using namespace csf;
        using namespace std::chrono;

        ConsensusParms parms;
        auto tg = TrustGraph::makeRingDirected(5, 4);
        Sim sim(
                parms,
                tg,
                topology(
                        tg,
                        fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)}));

        // everyone submits their own ID as a TX and relay it to peers
        for (auto& p : sim.peers)
            p.submit(Tx(static_cast<std::uint32_t>(p.id)));

        sim.run(10);

        // All peers are in sync
        if (BEAST_EXPECT(sim.synchronized()))
        {
            // Inspect the first node's state
            auto const & lcl = sim.peers.front().lastClosedLedger.get();
            BEAST_EXPECT(lcl.id() == sim.peers.front().prevLedgerID());
            BEAST_EXPECT(lcl.seq() == Ledger::Seq{10});
            // All peers proposed
            BEAST_EXPECT(
                    sim.peers.front().prevProposers() == sim.peers.size() - 1);
            // All transactions were accepted
            for (std::uint32_t i = 0; i < sim.peers.size(); ++i)
                BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());
        }

    }

    void
    run() override
    {
        testPeersAgree();
    }

};

BEAST_DEFINE_TESTSUITE(RingDirected, consensus, ripple);
} // test
} // ripple