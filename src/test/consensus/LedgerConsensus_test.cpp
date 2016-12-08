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
#include <ripple/consensus/ConsensusProposal.h>
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

class LedgerConsensus_test : public beast::unit_test::suite
{
public:
    using Time = std::chrono::steady_clock::time_point;
    using NodeID = std::int32_t;

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
    class Txn
    {
    public:
        using ID = int;

        Txn(ID i) : id_{ i } {}

        ID
        id() const
        {
            return id_;
        }

        bool
        operator<(Txn const & o) const
        {
            return id_ < o.id_;
        }

        bool
        operator==(Txn const & o) const
        {
            return id_ == o.id_;
        }

    private:
        ID id_;

    };

    //!-------------------------------------------------------------------------
    // All sets of Tx are represented as a flat_set.
    using TxSetType = bc::flat_set<Txn>;

    // TxSet is a set of transactions to consider including in the ledger
    class TxSet
    {
    public:
        using ID = TxSetType;
        using Tx = Txn;

        TxSet() = default;
        TxSet(TxSetType const & s) : txs_{ s } {}

        bool
        insert(Tx const & t)
        {
            return txs_.insert(t).second;
        }

        bool
        erase(Tx::ID const & txId)
        {
            return txs_.erase(Tx{ txId }) > 0;
        }

        bool
        exists(Tx::ID const txId) const
        {
            auto it = txs_.find(Tx{ txId });
            return it != txs_.end();
        }

        Tx const *
        find(Tx::ID const& txId) const
        {
            auto it = txs_.find(Tx{ txId });
            if (it != txs_.end())
                return &(*it);
            return nullptr;
        }

        auto const &
        id() const
        {
            return txs_;
        }

        // @return map of Tx::ID that are missing
        // true means it was in this set and not other
        // false means it was in the other set and not this
        std::map<Tx::ID, bool>
        diff(TxSet const& other) const
        {
            std::map<Tx::ID, bool> res;

            auto populate_diffs = [&res](auto const & a, auto const & b, bool s)
            {
                auto populator = [&](auto const & tx)
                {
                            res[tx.id()] = s;
                };
                std::set_difference(
                    a.begin(), a.end(),
                    b.begin(), b.end(),
                    boost::make_function_output_iterator(
                        std::ref(populator)
                    )
                );
            };

            populate_diffs(txs_, other.txs_, true);
            populate_diffs(other.txs_, txs_, false);
            return res;
        }

        // The set contains the actual transactions
        TxSetType txs_;

    };

    // A ledger is a set of observed transactions and a sequence number
    // identifying the ledger.
    class Ledger
    {
    public:

        struct ID
        {
            std::uint32_t seq = 0;
            TxSetType txs = TxSetType{};

            bool operator==(ID const & o) const
            {
                return seq == o.seq && txs == o.txs;
            }

            bool operator!=(ID const & o) const
            {
                return !(*this == o);
            }

            bool operator<(ID const & o) const
            {
                return std::tie(seq, txs) < std::tie(o.seq, o.txs);
            }
        };


        auto const &
        id() const
        {
            return id_;
        }

        auto
        seq() const
        {
            return id_.seq;
        }

        auto
        closeTimeResolution() const
        {
            return closeTimeResolution_;
        }

        auto
        closeAgree() const
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

        auto const &
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


        // Apply the given transactions to this ledger
        Ledger
        close(TxSetType const & txs,
            typename Time::duration closeTimeResolution,
            Time const & consensusCloseTime,
            bool closeTimeAgree) const
        {
            Ledger res{ *this };
            res.id_.txs.insert(txs.begin(), txs.end());
            res.id_ .seq= seq() + 1;
            res.closeTimeResolution_ = closeTimeResolution;
            res.closeTime_ = consensusCloseTime;
            res.closeTimeAgree_ = closeTimeAgree;
            res.parentCloseTime_ = closeTime();
            res.parentID_ = id();
            return res;
        }


    private:

        // Unique identifier of ledger is combination of sequence number and id
        ID id_;

        // Bucket resolution used to determine close time
        typename Time::duration closeTimeResolution_ = ledgerDefaultTimeResolution;

        // When the ledger closed
        Time closeTime_;

        // Whether consenssus agreed on the close time
        bool closeTimeAgree_ = true;

        // Parent ledger id
        ID parentID_;

        // Parent ledger close time
        Time parentCloseTime_;

    };

    // Position is a peer proposal in the consensus process and is represented
    // directly from the generic types
    using Proposal = ConsensusProposal<NodeID, Ledger::ID,
        TxSetType, Time>;

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


    struct Peer;

    // Collect the set of concrete consensus types in a Traits class.
    struct Traits
    {

        using Callbacks_t = Peer;
        // For testing, network and internal time of the consensus process
        // are the same.  In the RCL, the internal time and network time differ.
        using NetTime_t = Time;
        using Ledger_t = Ledger;
        using Proposal_t = Proposal;
        using TxSet_t = TxSet;
        using MissingTxException_t = MissingTx;
    };

    using Consensus = LedgerConsensus<Traits>;

    using Network = BasicNetwork<Peer*>;

    // Represents a single node participating in the consensus process
    // It implements the Callbacks required by Consensus and
    // owns/drives the Consensus instance.
    struct Peer
    {
        // Our unique ID
        NodeID id;
        // Journal needed for consensus debugging
        std::map<std::string, beast::Journal> j;
        // last time a ledger closed
        Time lastCloseTime;

        // openTxs that haven't been closed in a ledger yet
        TxSetType openTxs;

        // last ledger this peer closed
        Ledger lastClosedLedger;
        // Handle to network for sending messages
        Network & net;

        // The ledgers, proposals, TxSets and Txs this peer has seen
        bc::flat_map<Ledger::ID, Ledger> ledgers;
        // Map from Ledger::ID to vector of Positions with that ledger
        // as the prior ledger
        bc::flat_map<Ledger::ID, std::vector<Proposal>> proposals_;
        bc::flat_map<TxSet::ID, TxSet> txSets;
        bc::flat_set<Txn::ID> seenTxs;

        // Instance of Consensus
        std::shared_ptr<Consensus> consensus;

        int completedRounds = 0;
        int targetRounds = std::numeric_limits<int>::max();
        const std::chrono::milliseconds timerFreq = 100ms;

        // All peers start from the default constructed ledger
        Peer(NodeID i, Network & n) : id{i}, net{n}
        {
            consensus = std::make_shared<Consensus>(*this, net.clock());
            ledgers[lastClosedLedger.id()] = lastClosedLedger;
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

        Ledger const *
        acquireLedger(Ledger::ID const & ledgerHash)
        {
            auto it = ledgers.find(ledgerHash);
            if (it != ledgers.end())
                return &(it->second);
            // TODO: acquire from network?
            return nullptr;
        }

        auto const &
        proposals(Ledger::ID const & ledgerHash)
        {
            return proposals_[ledgerHash];
        }

        TxSet const *
        acquireTxSet(Proposal const & proposal)
        {
            // Weird . . should getPosition() type really be TxSet_idtype?
            auto it = txSets.find(proposal.getPosition());
            if(it != txSets.end())
                return &(it->second);
            // TODO Ask network for it?
            return nullptr;
        }


        bool
        hasOpenTransactions() const
        {
            return !openTxs.empty();
        }

        int
        numProposersValidated(Ledger::ID const & prevLedger)
        {
            // everything auto-validates, so just count the number of peers
            // who have this as the last closed ledger
            int res = 0;
            net.bfs(this, [&](auto, Peer * p)
            {
                if (this == p) return;
                if (p->lastClosedLedger.id() == prevLedger)
                    res++;
            });
            return res;
        }

        int
        numProposersFinished(Ledger::ID const & prevLedger)
        {
            // everything auto-validates, so just count the number of peers
            // who have this as a PRIOR ledger
            int res = 0;
            net.bfs(this, [&](auto, Peer * p)
            {
                if (this == p) return;
                auto const & pLedger = p->lastClosedLedger.id();
                // prevLedger preceeds pLedger iff it has a smaller
                // sequence number AND its Tx's are a subset of pLedger's
                if(prevLedger.seq < pLedger.seq
                    && std::includes(pLedger.txs.begin(), pLedger.txs.end(),
                        prevLedger.txs.begin(), prevLedger.txs.end()))
                {
                    res++;
                }
            });
            return res;
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
        dispatchAccept(F && f)
        {
            int dummy = 1;
            f(dummy);
        }

        void
        share(TxSet const &s)
        {
            relay(s);
        }

        Ledger::ID
        getLCL(Ledger::ID const & currLedger,
            Ledger::ID const & priorLedger,
            bool haveCorrectLCL)
        {
            // TODO: cases where this peer is behind others ?
            return lastClosedLedger.id();
        }

        void
        propose(Proposal const & pos)
        {
            relay(pos);
        }

        void
        relay(Consensus::Dispute_t const & dispute)
        {
            relay(dispute.tx());
        }

        std::pair <TxSet, Proposal>
        makeInitialPosition(
                Ledger const & prevLedger,
                bool isProposing,
                bool isCorrectLCL,
                Time closeTime,
                Time now)
        {
            TxSet res{ openTxs };

            return { res,
                Proposal{prevLedger.id(), res.id(), closeTime, now, id} };
        }

        // Process the accepted transaction set, generating the newly closed ledger
        // and clearing out hte openTxs that were included.
        // TODO: Kinda nasty it takes so many arguments . . . sign of bad coupling
        void
        accept(TxSet const& set,
            Time consensusCloseTime,
            bool proposing_,
            bool & validating_,
            bool haveCorrectLCL_,
            bool consensusFail_,
            Ledger::ID const & prevLedgerHash_,
            Ledger const & previousLedger_,
            Time::duration closeResolution_,
            Time const & now,
            std::chrono::milliseconds const & roundTime_,
            hash_map<Txn::ID, DisputedTx <Txn, NodeID>> const & disputes_,
            std::map <Time, int> closeTimes_,
            Time const & closeTime,
            Json::Value && json)
        {
            auto newLedger = previousLedger_.close(set.txs_, closeResolution_,
                closeTime, consensusCloseTime != Time{});
            ledgers[newLedger.id()] = newLedger;

            lastClosedLedger = newLedger;

            auto it = std::remove_if(openTxs.begin(), openTxs.end(),
                [&](Txn const & tx)
                {
                    return set.exists(tx.id());
                });
            openTxs.erase(it, openTxs.end());
        }

        void
        endConsensus(bool correct)
        {
           // kick off the next round...
           // in the actual implementation, this passes back through
           // network ops
           ++completedRounds;
           // startRound sets the LCL state, so we need to call it once after
           // the last requested round completes
           if(completedRounds <= targetRounds)
           {
             consensus->startRound(net.now(), lastClosedLedger.id(),
                    lastClosedLedger);
           }
        }

        //-------------------------------------------------------------------------
        // non-callback helpers
        void
        receive(Proposal const & p)
        {
            // filter proposals already seen?
            proposals_[p.getPrevLedgerID()].push_back(p);
            consensus->peerProposal(net.now(), p);

        }

        void
        receive(TxSet const & txs)
        {
            // save and map complete?
            auto it = txSets.insert(std::make_pair(txs.id(), txs));
            if(it.second)
                consensus->gotMap(net.now(), txs);
        }

        void
        receive(Txn const & tx)
        {
            if (seenTxs.find(tx.id()) == seenTxs.end())
            {
                openTxs.insert(tx);
                seenTxs.insert(tx.id());
            }
        }

        template <class T>
        void
        relay(T const & t)
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
        submit(Txn const & tx)
        {
            receive(tx);
            relay(tx);
        }

        void
        timerEntry()
        {
            consensus->timerEntry(net.now());
            // only reschedule if not completed
            if(completedRounds < targetRounds)
                net.timer(timerFreq, [&]() { timerEntry(); });
        }
        void
        start()
        {
            consensus->startRound(net.now(), lastClosedLedger.id(),
                lastClosedLedger);
        }
    };
    void
    testStandalone()
    {
        Network n;
        Peer p{ 0, n };

        p.targetRounds = 1;
        p.start();
        p.receive(Txn{ 1 });

        n.step();

        // Inspect that the proper ledger was created
        BEAST_EXPECT(p.consensus->getLCL().seq == 1);
        BEAST_EXPECT(p.consensus->getLCL() == p.lastClosedLedger.id());
        BEAST_EXPECT(p.lastClosedLedger.id().txs.size() == 1);
        BEAST_EXPECT(p.lastClosedLedger.id().txs.find(Txn{ 1 })
            != p.lastClosedLedger.id().txs.end());
        BEAST_EXPECT(p.consensus->getLastCloseProposers() == 0);
    }


    void
    run_consensus(Network & n, std::vector<Peer> & peers, int rounds)
    {
        for(auto & p : peers)
            p.targetRounds = p.completedRounds + rounds;
        n.step();
    }

    void
    testPeersAgree()
    {
        Network n;
        std::vector<Peer> peers;
        peers.reserve(5);

        for (int i = 0; i < 5; ++i)
        {
            peers.emplace_back(i, n);
        }

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
            p.submit(Txn{ p.id });
        }

        // Let consensus proceed for one round
        run_consensus(n, peers, 1);

        // Verify all peers have same LCL and it has all the TXs
        for (auto & p : peers)
        {
            auto const &lgrID = p.consensus->getLCL();
            BEAST_EXPECT(lgrID.seq == 1);
            BEAST_EXPECT(p.consensus->getLastCloseProposers() == peers.size() - 1);
            for(int i = 0; i < peers.size(); ++i)
                BEAST_EXPECT(lgrID.txs.find(Txn{ i }) != lgrID.txs.end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lgrID.txs == peers[0].consensus->getLCL().txs);
        }


    }

    void
    testSlowPeer()
    {
        Network n;
        std::vector<Peer> peers;
        peers.reserve(5);
        for (int i = 0; i < 5; ++i)
        {
            peers.emplace_back(i, n);
        }

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
            p.submit(Txn{ p.id });
        }

        // Let consensus proceed, only 1 round needed
        // since all but the slow peer have 0 delay
        run_consensus(n, peers, 1);

        // Verify all peers have same LCL but are missing transaction 0 which
        // was not received by all peers before the ledger closed
        for (auto & p : peers)
        {
            using namespace std::chrono;

            auto const &lgrID = p.consensus->getLCL();
            BEAST_EXPECT(lgrID.seq == 1);
            BEAST_EXPECT(p.consensus->getLastCloseProposers() == peers.size() - 1);
            // Peer 0 closes first because it sees a quorum of agreeing positions
            // from all other peers in one hop (1->0, 2->0, ..)
            // The other peers take an extra timer period before they find that
            // Peer 0 agrees with them ( 1->0->1,  2->0->2, ...)
            if(p.id != 0)
                BEAST_EXPECT(p.consensus->getLastCloseDuration()
                    > peers[0].consensus->getLastCloseDuration());

            BEAST_EXPECT(lgrID.txs.find(Txn{ 0 }) == lgrID.txs.end());
            for(int i = 1; i < peers.size(); ++i)
                BEAST_EXPECT(lgrID.txs.find(Txn{ i }) != lgrID.txs.end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lgrID.txs == peers[0].consensus->getLCL().txs);
        }
        BEAST_EXPECT(peers[0].openTxs.find(Txn{ 0 }) != peers[0].openTxs.end());
    }

    void
    run() override
    {
        testStandalone();
        testPeersAgree();
        testSlowPeer();
    }
};

inline std::ostream& operator<<(std::ostream & o, LedgerConsensus_test::TxSetType const & txs)
{
    o << "{ ";
    bool do_comma = false;
    for (auto const & tx : txs)
    {
        if (do_comma)
            o << ", ";
        else
            do_comma = true;
        o << tx.id();


    }
    o << " }";
    return o;

}

inline std::string to_string(LedgerConsensus_test::TxSetType const & txs)
{
    std::stringstream ss;
    ss << txs;
    return ss.str();
}

inline std::ostream & operator<<(std::ostream & o, LedgerConsensus_test::Ledger::ID const & id)
{
    return o << id.seq << "," << id.txs;
}

inline std::string to_string(LedgerConsensus_test::Ledger::ID const & id)
{
    std::stringstream ss;
    ss << id;
    return ss.str();
}

std::ostream& operator<< (std::ostream & o, LedgerConsensus_test::MissingTx const &m)
{
    return o << m.what();
}

template <class Hasher>
void
inline hash_append(Hasher& h, LedgerConsensus_test::Txn const & tx)
{
    using beast::hash_append;
    hash_append(h, tx.id());
}
BEAST_DEFINE_TESTSUITE(LedgerConsensus, consensus, ripple);
} // test
} // ripple