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
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/LedgerConsensus.h>
#include <ripple/consensus/ConsensusPosition.h>
#include <ripple/test/BasicNetwork.h>
#include <ripple/beast/clock/manual_clock.h>
#include <boost/container/flat_set.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/function_output_iterator.hpp>
#include <ripple/beast/hash/hash_append.h>
#include <utility>

namespace ripple {
namespace test {

namespace bc = boost::container;
using clock_type = std::chrono::steady_clock;
using time_point = typename clock_type::time_point;
using node_id_type = std::int32_t;

/** Consensus unit test types

    Peers in the consensus process are trying to agree on a set of transactions
    to include in a ledger.  For unit testing, each transaction is a
    single integer and the ledger is a set of observed integers.  This means
    future ledgers have prior ledgers as subsets, e.g.

        Ledger 0 :  {}
        Ledger 1 :  {1,4,5}
        Ledger 2 :  {1,2,4,5,10}
        ....

    Tx - Integer
    TxSet/MutableTxSet - Set of Tx
    Ledger - Set of Tx and sequence number

*/


// A single transaction
class Tx
{
public:
    using id_type = int;

    Tx(id_type i) : id{ i } {}

    id_type
    getID() const
    {
        return id;
    }

    bool
    operator<(Tx const & o) const
    {
        return id < o.id;
    }

    bool
    operator==(Tx const & o) const
    {
        return id == o.id;
    }

private:
    id_type id;

};

template <class Hasher>
void
inline hash_append(Hasher& h, Tx const & tx)
{
    using beast::hash_append;
    hash_append(h, tx.getID());
}

//!-------------------------------------------------------------------------
// All sets of Tx are represented as a flat_set.
using tx_set_type = bc::flat_set<Tx>;

inline std::ostream& operator<<(std::ostream & o, tx_set_type const & txs)
{
    o << "{ ";
    bool do_comma = false;
    for (auto const & tx : txs)
    {
        if (do_comma)
            o << ", ";
        else
            do_comma = true;
        o << tx.getID();


    }
    o << " }";
    return o;

}

inline std::string to_string(tx_set_type const & txs)
{
    std::stringstream ss;
    ss << txs;
    return ss.str();
}

// TxSet/MutableTxSet are a set of transactions to consider including in the
// ledger
class TxSet;

class MutableTxSet
{
public:
    friend class TxSet;

    MutableTxSet() = default;

    MutableTxSet(TxSet const &);

    bool
    insert(Tx const & t)
    {
        return txs.insert(t).second;
    }

    bool
    remove(Tx::id_type const & tx_id)
    {
        return txs.erase(Tx{ tx_id }) > 0;
    }

private:
    // The set contains the actual transactions
    tx_set_type txs;

};

class TxSet
{
public:
    friend class MutableTxSet;

    using id_type = tx_set_type;
    using tx_type = Tx;
    using mutable_t = MutableTxSet;

    TxSet() = default;
    TxSet(tx_set_type const & s) : txs{ s } {}
    TxSet(MutableTxSet const & s)
        : txs{ s.txs }
    {

    }

    bool
    hasEntry(Tx::id_type const tx_id) const
    {
        auto it = txs.find(Tx{ tx_id });
        return it != txs.end();
    }

    boost::optional <Tx const>
    getEntry(Tx::id_type const& tx_id) const
    {
        auto it = txs.find(Tx{ tx_id });
        if (it != txs.end())
            return *it;
        return boost::none;
    }

    auto
    getID() const
    {
        return txs;
    }

    // @return map of Tx::id_type that are missing
    // true means it was in this set and not other
    // false means it was in the other set and not this
    std::map<Tx::id_type, bool>
    getDifferences(TxSet const& other) const
    {
        std::map<Tx::id_type, bool> res;

        auto populate_diffs = [&res](auto const & a, auto const & b, bool s)
        {
            auto populator = [&](auto const & tx)
            {
                        res[tx.getID()] = s;
            };
            std::set_difference(
                a.begin(), a.end(),
                b.begin(), b.end(),
                boost::make_function_output_iterator(
                    std::ref(populator)
                )
            );
        };

        populate_diffs(txs, other.txs, true);
        populate_diffs(other.txs, txs, false);
        return res;
    }

    auto const &
    peek() const
    {
        return txs;
    }


private:
    // The set contains the actual transactions
    tx_set_type txs;

};

MutableTxSet::MutableTxSet(TxSet const & s)
    : txs(s.txs) {}

// A ledger is a set of observed transactions and a sequence number
// identifying the ledger.
class Ledger
{
public:

    using id_type = std::pair<std::uint32_t, tx_set_type>;

    id_type
    ID() const
    {
        return { seq_, txs_ };
    }

    auto
    seq() const
    {
        return seq_;
    }

    auto
    closeTimeResolution() const
    {
        return closeTimeResolution_;
    }

    auto
    getCloseAgree() const
    {
        return closeTimeAgree_;
    }

    auto
    closeTime() const
    {
        return closeTime_;
    }

    auto
    parentCloseTime() const
    {
        return parentCloseTime_;
    }

    auto
    parentID() const
    {
        return parentID_;
    }

    Json::Value
    getJson() const
    {
        Json::Value res(Json::objectValue);
        res["seq"] = seq();
        return res;
    }

    auto const &
    peek() const
    {
        return txs_;
    }

    // Apply the given transactions to this ledger
    Ledger
    close(tx_set_type const & txs,
        typename time_point::duration closeTimeResolution,
        time_point const & consensusCloseTime,
        bool closeTimeAgree) const
    {
        Ledger res{ *this };
        res.txs_.insert(txs.begin(), txs.end());
        res.seq_ = seq() + 1;
        res.closeTimeResolution_ = closeTimeResolution;
        res.closeTime_ = consensusCloseTime;
        res.closeTimeAgree_ = closeTimeAgree;
        res.parentCloseTime_ = closeTime();
        res.parentID_ = ID();
        return res;
    }

private:

    // Set of transactions in the ledger
    tx_set_type txs_;
    // Sequence number of the ledger
    std::int32_t seq_ = 0;
    // Bucket resolution used to determine close time
    typename time_point::duration closeTimeResolution_ = ledgerDefaultTimeResolution;
    // When the ledger closed
    time_point closeTime_;
    // Whether consenssus agreed on the close time
    bool closeTimeAgree_ = true;

    // Parent ledger id
    id_type parentID_;
    // Parent ledger close time
    time_point parentCloseTime_;

};


inline std::ostream & operator<<(std::ostream & o, Ledger::id_type const & id)
{
    return o << id.first << "," << id.second;
}

inline std::string to_string(Ledger::id_type const & id)
{
    std::stringstream ss;
    ss << id;
    return ss.str();
}

// Position is a peer proposal in the consensus process and is represented
// directly from the generic types
using Position = ConsensusPosition<node_id_type, Ledger::id_type,
    tx_set_type, time_point>;

// The RCL consensus process catches missing node SHAMap error
// in several points. This exception is meant to represent a similar
// case for the unit test.
class MissingTx : public std::runtime_error
{
public:
    MissingTx()
        : std::runtime_error("MissingTx")
    {}

    friend std::ostream& operator<< (std::ostream&, MissingTx const&);
};

std::ostream& operator<< (std::ostream & o, MissingTx const &m)
{
    return o << m.what();
}

struct Peer;

// Collect the set of concrete consensus types in a Traits class.
struct Traits
{
    using Callback_t = Peer;
    // For testing, network and internal time of the consensus process
    // are the same.  In the RCL, the internal time and network time differ.
    using NetTime_t = time_point;
    using Ledger_t = Ledger;
    using Pos_t = Position;
    using TxSet_t = TxSet;
    using MissingTx_t = MissingTx;
};

using Consensus = LedgerConsensus<Traits>;

using Network = BasicNetwork<Peer*>;

// Represents a single node participating in the consensus process
// It implements the Callbacks required by Consensus and
// owns/drives the Consensus instance.
struct Peer
{
    // Our unique ID
    Position::node_id_type id;
    // Journal needed for consensus debugging
    std::map<std::string, beast::Journal> j;
    // last time a ledger closed
    time_point lastCloseTime;

    // openTxs that haven't been closed in a ledger yet
    tx_set_type openTxs;

    // last ledger this peer closed
    Ledger lastClosedLedger;
    // Handle to network for sending messages
    Network & net;

    // The ledgers, proposals, TxSets and Txs this peer has seen
    bc::flat_map<Ledger::id_type, Ledger> ledgers;
    // Map from Ledger::id_type to vector of Positions with that ledger
    // as the prior ledger
    bc::flat_map<Ledger::id_type, std::vector<Position>> proposals;
    bc::flat_map<TxSet::id_type, TxSet> txSets;
    bc::flat_set<Tx::id_type> seenTxs;

    // Instance of Consensus
    std::shared_ptr<Consensus> consensus;

    const std::chrono::seconds timerFreq = 1s;

    // All peers start from the default constructed ledger
    Peer(Position::node_id_type i, Network & n) : id{i}, net{n}
    {
        consensus = std::make_shared<Consensus>(*this, net.clock());
        ledgers[lastClosedLedger.ID()] = lastClosedLedger;
        lastCloseTime = lastClosedLedger.closeTime();
        net.timer(timerFreq, [&]() { timerEntry(); });
    }


    beast::Journal
    journal(std::string const & s)
    {
        return j[s];
    }

    // @return whether we are validating,proposing
    // TODO: Bit akward that this is in callbacks, would be nice to extract
    std::pair<bool, bool>
    getMode(const bool correctLCL)
    {
        if (!correctLCL)
            return{ false, false };
        return{ true, true };
    }

    boost::optional<Ledger>
    acquireLedger(Ledger::id_type const & ledgerHash)
    {
        auto it = ledgers.find(ledgerHash);
        if (it != ledgers.end())
            return it->second;
        // TODO: acquire from network?
        return boost::none;
    }

    // Should be named get and share?
    // If f returns true, that means it was
    // a useful proposal and should be shared
    template <class F>
    void
    getProposals(Ledger::id_type const & ledgerHash, F && f)
    {
        for (auto const & proposal : proposals[ledgerHash])
        {
            if (f(proposal))
            {
                relay(proposal);
            }
        }
    }

    boost::optional<TxSet>
    getTxSet(Position const & position)
    {
        // Weird . . should getPosition() type really be TxSet_idtype?
        auto it = txSets.find(position.getPosition());
        if(it != txSets.end())
            return it->second;
        // TODO Ask network for it?
        return boost::none;
    }


    bool
    hasOpenTransactions() const
    {
        return !openTxs.empty();
    }

    int
    numProposersValidated(Ledger::id_type const & prevLedger)
    {
        // everything auto-validates, so just count the number of peers
        // who have this as the last closed ledger
        int res = 0;
        net.bfs(this, [&](auto, Peer * p)
        {
            if (this == p) return;
            if (p->lastClosedLedger.ID() == prevLedger)
                res++;
        });
        return res;
    }

    int
    numProposersFinished(Ledger::id_type const & prevLedger)
    {
        // everything auto-validates, so just count the number of peers
        // who have this as a PRIOR ledger
        int res = 0;
        net.bfs(this, [&](auto, Peer * p)
        {
            if (this == p) return;
            auto const & pLedger = p->lastClosedLedger.ID();
            // prevLedger preceeds pLedger iff it has a smaller
            // sequence number AND its Tx's are a subset of pLedger's
            if(prevLedger.first < pLedger.first
                && std::includes(pLedger.second.begin(), pLedger.second.end(),
                    prevLedger.second.begin(), prevLedger.second.end()))
            {
                res++;
            }
        });
        return res;
    }

    time_point
    getLastCloseTime() const
    {
        return lastCloseTime;
    }

    void
    setLastCloseTime(time_point tp)
    {
        lastCloseTime = tp;
    }

    void
    statusChange(ConsensusChange c, Ledger const & prevLedger,
        bool haveCorrectLCL)
    {

    }


    // don't really offload
    // TODO: Should not be imposed on clients?
    template <class F>
    void
    offloadAccept(F && f)
    {
        int dummy = 1;
        f(dummy);
    }

    void
    shareSet(TxSet const &s)
    {
        relay(s);
    }

    Ledger::id_type
    getLCL(Ledger::id_type const & currLedger,
        Ledger::id_type const & priorLedger,
        bool haveCorrectLCL)
    {
        // TODO: cases where this peer is behind others ?
        return lastClosedLedger.ID();
    }

    void
    propose(Position const & pos)
    {
        relay(pos);
    }


    // Process the accepted transaction set, generating the newly closed ledger
    // and clearing out hte openTxs that were included.
    // TODO: Kinda nasty it takes so many arguments . . . sign of bad coupling
    void
    accept(TxSet const& set,
        time_point consensusCloseTime,
        bool proposing_,
        bool & validating_,
        bool haveCorrectLCL_,
        bool consensusFail_,
        Ledger::id_type const & prevLedgerHash_,
        Ledger const & previousLedger_,
        time_point::duration closeResolution_,
        time_point const & now,
        std::chrono::milliseconds const & roundTime_,
        hash_map<Tx::id_type, DisputedTx <Tx, node_id_type>> const & disputes_,
        std::map <time_point, int> closeTimes_,
        time_point const & closeTime,
        Json::Value && json)
    {
        auto newLedger = previousLedger_.close(set.peek(), closeResolution_,
            closeTime, consensusCloseTime != time_point{});
        ledgers[newLedger.ID()] = newLedger;

        lastClosedLedger = newLedger;

        auto it = std::remove_if(openTxs.begin(), openTxs.end(),
            [&](Tx const & tx)
            {
                return set.hasEntry(tx.getID());
            });
        openTxs.erase(it, openTxs.end());
    }

    void
    relayDisputedTx(Tx const &tx)
    {
        relay(tx);
    }

    void
    endConsensus(bool correct)
    {
       // kick off the next round...
       // in the actual implementation, this passes back through
       // network ops
       consensus->startRound(net.now(), lastClosedLedger.ID(),
            lastClosedLedger);
    }

    std::pair <TxSet, Position>
    makeInitialPosition(
            Ledger const & prevLedger,
            bool isProposing,
            bool isCorrectLCL,
            time_point closeTime,
            time_point now)
    {
        TxSet res{ openTxs };

        return { res,
            Position{prevLedger.ID(), res.getID(), closeTime, now, id} };
    }

    //-------------------------------------------------------------------------
    // non-callback helpers
    void
    receive(Position const & p)
    {
        // filter proposals already seen?
        proposals[p.getPrevLedger()].push_back(p);
        consensus->peerPosition(net.now(), p);

    }

    void
    receive(TxSet const & txs)
    {
        // save and map complete?
        auto it = txSets.insert(std::make_pair(txs.getID(), txs));
        if(it.second)
            consensus->gotMap(net.now(), txs);
    }

    void
    receive(Tx const & tx)
    {
        if (seenTxs.find(tx.getID()) == seenTxs.end())
        {
            openTxs.insert(tx);
            seenTxs.insert(tx.getID());
        }
    }

    template <class T>
    void
    relay(T && t)
    {
        for(auto const& link : net.links(this))
            net.send(this, link.to,
                [&, msg = t, to = link.to]
                {
                    to->receive(msg);
                });
    }

    // Receive a locally submitted transaction and
    // share with peers
    void
    submit(Tx const & tx)
    {
        receive(tx);
        relay(tx);
    }

    void
    timerEntry()
    {
        consensus->timerEntry(net.now());
        net.timer(timerFreq, [&]() { timerEntry(); });
    }
    void
    start()
    {
        consensus->startRound(net.now(), lastClosedLedger.ID(),
            lastClosedLedger);
    }
};


class LedgerConsensus_test : public beast::unit_test::suite
{
    void
    testStandalone()
    {
        Network n;
        Peer p{ 0, n };
        n.step_for(9s);
        p.start();
        p.receive(Tx{ 1 });
        n.step_for(2s);

        // not enough time has elapsed to close the ledger
        BEAST_EXPECT(p.consensus->getLCL().first == 0);

        // advance enough to close and accept and start the next round
        n.step_for(7s);

        // Inspect that the proper ledger was created
        BEAST_EXPECT(p.consensus->getLCL().first == 1);
        BEAST_EXPECT(p.consensus->getLCL() == p.lastClosedLedger.ID());
        BEAST_EXPECT(p.lastClosedLedger.peek().size() == 1);
        BEAST_EXPECT(p.lastClosedLedger.peek().find(Tx{ 1 })
            != p.lastClosedLedger.peek().end());
        BEAST_EXPECT(p.consensus->getLastCloseDuration() == 8s);
        BEAST_EXPECT(p.consensus->getLastCloseProposers() == 0);
    }

    void
    testPeersAgree()
    {
        Network n;
        std::vector<Peer> peers;
        peers.reserve(5);
        for (int i = 0; i < 5; ++i)
            peers.emplace_back(i, n);

        // fully connect the graph?
        for (int i = 0; i < peers.size(); ++i )
            for (int j = 0; j < peers.size(); ++j)
            {
                if (i != j)
                    n.connect(&peers[i], &peers[j], 20ms * (i + 1));
            }

        // everyone submits their own ID as a TX
        for (auto & p : peers)
        {
            p.start();
            p.submit(Tx{ p.id });
        }

        // Let consensus proceed
        n.step_for(5s);

        // Verify all peers have same LCL and it has all the TXs
        for (auto & p : peers)
        {
            auto const &lgrID = p.consensus->getLCL();
            BEAST_EXPECT(lgrID.first == 1);
            BEAST_EXPECT(p.consensus->getLastCloseProposers() == 4);
            BEAST_EXPECT(p.consensus->getLastCloseDuration() == 3s);
            for(int i = 0; i < peers.size(); ++i)
                BEAST_EXPECT(lgrID.second.find(Tx{ i }) != lgrID.second.end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lgrID.second == peers[0].consensus->getLCL().second);
        }


    }

    void
    testSlowPeer()
    {
        Network n;
        std::vector<Peer> peers;
        peers.reserve(5);
        for (int i = 0; i < 5; ++i)
            peers.emplace_back(i, n);

        // Fully connected, but node 0 has a large delay
        for (int i = 0; i < peers.size(); ++i )
            for (int j = 0; j < peers.size(); ++j)
            {
                auto delay = (i == 0 || j == 0) ? 1100ms : 0ms;
                n.connect(&peers[i], &peers[j], delay);
            }

        // everyone submits their own ID as a TX
        for (auto & p : peers)
        {
            p.start();
            p.submit(Tx{ p.id });
        }

        // Let consensus proceed
        n.step_for(5s);

        // Verify all peers have same LCL but are missing transaction 0 which
        // was not received by all peers before the ledger closed
        for (auto & p : peers)
        {
            using namespace std::chrono;

            auto const &lgrID = p.consensus->getLCL();
            BEAST_EXPECT(lgrID.first == 1);
            BEAST_EXPECT(p.consensus->getLastCloseProposers() == 4);
            // Peer 0 closes first because it sees a quorum of agreeing positions
            // from all other peers in one hop (1->0, 2->0, ..)
            // The other peers take an extra timer period before they find that
            // Peer 0 agrees with them ( 1->0->1,  2->0->2, ...)
            if(&p == &peers[0])
                BEAST_EXPECT(p.consensus->getLastCloseDuration() == 3s);
            else
                BEAST_EXPECT(p.consensus->getLastCloseDuration() == 4s);
            BEAST_EXPECT(lgrID.second.find(Tx{ 0 }) == lgrID.second.end());
            for(int i = 1; i < peers.size(); ++i)
                BEAST_EXPECT(lgrID.second.find(Tx{ i }) != lgrID.second.end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lgrID.second == peers[0].consensus->getLCL().second);
        }
        BEAST_EXPECT(peers[0].openTxs.find(Tx{ 0 }) != peers[0].openTxs.end());
    }


    void
    testGetJson()
    {

    }
    void
    run() override
    {
        testStandalone();
        testPeersAgree();
        testSlowPeer();
        testGetJson();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerConsensus, consensus, ripple);
} // test
} // ripple