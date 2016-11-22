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

using clock_type = std::chrono::steady_clock;
using time_point = typename clock_type::time_point;
using node_id_type = std::int32_t;

/** Consensus test framework

    For unit tests @b LedgerConsensus, we define

    Tx : integer
    TxSet : set of integers
    Ledger : set of integers
    Pos :
*/

class Tx
{
public:
    using id_type = int;

    Tx(id_type i) : id{ i } {}

    id_type getID() const
    {
        return id;
    }

    bool operator<(Tx const & o) const
    {
        return id < o.id;
    }

    bool operator==(Tx const & o) const
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


using tx_set_type = boost::container::flat_set<Tx>;


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

class TxSet;

class MutableTxSet
{
public:
    friend class TxSet;

    MutableTxSet() = default;

    MutableTxSet(TxSet const &);

    bool insert(Tx const & t)
    {
        return txs.insert(t).second;
    }

    bool remove(Tx::id_type const & tx_id)
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

    // For the test, use the same object for mutable/immutable
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

    auto getID() const
    {
        return txs;
    }

    auto peek() const
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

private:
    // The set contains the actual transactions
    tx_set_type txs;

};

MutableTxSet::MutableTxSet(TxSet const & s)
    : txs(s.txs) {}

class Ledger
{
public:

    using id_type = std::pair<std::uint32_t, tx_set_type>;

    id_type ID() const
    {
        return { seq_, txs_ };
    }

    auto seq() const
    {
        return seq_;
    }

    auto closeTimeResolution() const
    {
        return closeTimeResolution_;
    }

    auto getCloseAgree() const
    {
        return closeTimeAgree_;
    }

    auto closeTime() const
    {
        return closeTime_;
    }

    auto parentCloseTime() const
    {
        return parentCloseTime_;
    }

    auto parentID() const
    {
        return parentID_;
    }

    Json::Value getJson() const
    {
        Json::Value res(Json::objectValue);
        res["seq"] = seq();
        return res;
    }


    auto const & peek() const
    {
        return txs_;
    }

    Ledger close(tx_set_type const & txs,
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

    tx_set_type txs_;
    std::int32_t seq_ = 0;
    typename time_point::duration closeTimeResolution_ = ledgerDefaultTimeResolution;
    time_point closeTime_;
    bool closeTimeAgree_ = true;

    time_point parentCloseTime_;
    id_type parentID_;
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


using Position = ConsensusPosition<node_id_type, Ledger::id_type,
    tx_set_type, time_point>;

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

struct Callbacks;

struct Traits
{
    using Callback_t = Callbacks;
    using NetTime_t = time_point;
    using Ledger_t = Ledger;
    using Pos_t = Position;
    using TxSet_t = TxSet;
    using MissingTx_t = MissingTx;
};

using Consensus = LedgerConsensus<Traits>;
struct Peer;

using Network = BasicNetwork<Peer*>;

// Represents a single node participating in the consensus process
// and implements the Callbacks required by LedgerConsensus
struct Peer
{

    Position::node_id_type id;
    std::map<std::string, beast::Journal> j;

    tx_set_type openTxs;

    time_point lastCloseTime;
    boost::optional<ConsensusChange> lastStatusChange;
    Ledger lastClosedLedger;
    boost::container::flat_map<Ledger::id_type, Ledger> ledgers;
    Network * net = nullptr;

    std::map<Ledger::id_type, std::vector<Position>> proposals;

    // All peers start from the default constructed ledger
    Peer(Position::node_id_type i) : id{i}
    {
        ledgers[lastClosedLedger.ID()] = lastClosedLedger;
        lastCloseTime = lastClosedLedger.closeTime();
    }

    // Callback functions
    beast::Journal
    journal(std::string const & s)
    {
        return j[s];
    }

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

    // Should be get and share?
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
                 for(auto const& link : net.links(this))
                    net.send(this, link.to,
                        [&, id = this->id, msg = proposal, to = link.to]
                        {
                            to->receive(msg);
                        });
            }
        }
    }

    // Aquire the details of the transaction corresponding
    // to this position; if not available locally, spawns
    // a network request that will call gotMap
    boost::optional<TxSet>
    getTxSet(Position const & position)
    {
        std::cout << "getTxSet\n";
        return {};
    }


    bool
    hasOpenTransactions() const
    {
        return !openTxs.empty();
    }

    int
    numProposersValidated(Ledger::id_type const & prevLedger) const
    {
        std::cout << "numProposersValidated\n";
        return 0;
    }

    int
    numProposersFinished(Ledger::id_type const & prevLedger) const
    {
        std::cout << "numProposersFinished\n";
        return 0;
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
        lastStatusChange = c;
    }


    // don't really offload
    template <class F>
    void
    offloadAccept(F && f)
    {
        int dummy = 1;
        f(dummy);
    }

    void
    shareSet(TxSet const &)
    {
        std::cout << "shareSet\n";
    }

    Ledger::id_type
    getLCL(Ledger::id_type const & prevLedger,
        Ledger::id_type  const & prevParent,
        bool haveCorrectLCL)
    {
        // TODO: cases where this peer is behind others
        return lastClosedLedger.ID();
    }

    void
    propose(Position const & pos)
    {
        relay(pos);
    }

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

        lastStatusChange = ConsensusChange::Accepted;

        auto newLedger = previousLedger_.close(set.peek(), closeResolution_,
            closeTime, consensusCloseTime != time_point{});
        ledgers[newLedger.ID()] = newLedger;

        lastClosedLedger = newLedger;

        auto it = std::remove_if(openTxs.begin(), openTxs.end(), [&](Tx const & tx)
        {
            return set.hasEntry(tx.getID());
        });
        openTxs.erase(it, openTxs.end());

    }

    void
    relayDisputedTx(Tx const &)
    {
        std::cout << "relay\n";
    }

    void
    endConsensus(bool correct)
    {
       // kick off the next round...
       // in the actual implementation, this passes back through
       // network ops
        consensus->startRound(clock.now(), lastClosedLedger.ID(),
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

        return { res, Position{prevLedger.ID(), res.getID(), closeTime, now} };
    }

    //-------------------------------------------------------------------------
    // non-callback helpers
    void receive(Position const & p)
    {
        proposals[p.getPrevLedger()].push_back(p);
    }

    template <class T>
    void relay(T && t)
    {
        for(auto const& link : net.links(this))
            net.send(this, link.to,
                [&, msg = t, to = link.to]
                {
                    to->receive(t);
                });
    }
};


class LedgerConsensus_test : public beast::unit_test::suite
{
    void
    testStandalone()
    {
        //using namespace consensus;
        Callbacks cb;

        std::shared_ptr<Consensus> c = std::make_shared<Consensus>( cb, 0, cb.clock );
        cb.consensus = c;

        // No peers
        // Local transactions only
        // Always have ledger
        // Proposing and validating



        // 1. Genesis ledger
        Ledger currLedger;
        cb.clock.advance(10s);

        c->startRound(cb.clock.now(), currLedger.ID(), currLedger);
        BEAST_EXPECT(cb.lastStatusChange.get() == ConsensusChange::StartRound);


        cb.clock.advance(1s);
        cb.openTxs.insert(Tx{ 1 });
        c->timerEntry(cb.clock.now());
        // not enough time has elapsed to close the ledger
        BEAST_EXPECT(cb.lastStatusChange.get() == ConsensusChange::StartRound);

        // advance enough to close and accept and start the next round
        cb.clock.advance(7s);
        c->timerEntry(cb.clock.now());
        BEAST_EXPECT(cb.lastStatusChange.get() == ConsensusChange::StartRound);

        // Inspect that the proper ledger was created
        BEAST_EXPECT(c->getLCL() == cb.lastClosedLedger.ID());
        BEAST_EXPECT(cb.lastClosedLedger.peek().size() == 1);
        BEAST_EXPECT(cb.lastClosedLedger.peek().find(Tx{ 1 })
            != cb.lastClosedLedger.peek().end());
        BEAST_EXPECT(c->getLastCloseDuration() == 8s);
        BEAST_EXPECT(c->getLastCloseProposers() == 0);

    }

    void
    testPeersAgree()
    {

    }

    void
    testPeersDisagree()
    {

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

        testPeersDisagree();

        testGetJson();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerConsensus, consensus, ripple);
} // test
} // ripple