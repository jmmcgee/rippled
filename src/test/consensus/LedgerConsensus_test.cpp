//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <ripple/beast/clock/manual_clock.h>

#include <set>

namespace ripple {
namespace test {


class MissingTx : public std::runtime_error
{
public:
    MissingTx()
        : std::runtime_error("MissingTx")
    {}

    friend std::ostream& operator<< (std::ostream&, MissingTx const&);
};


std::ostream& operator<< (std::ostream& o, MissingTx const& mt)
{
    return o << mt.what();
}
class LedgerConsensus_test : public beast::unit_test::suite
{
    using clock_type =
        beast::manual_clock<
            NetClock>;


    struct Traits
    {

        using id_t = std::int32_t;

        using LgrID_t = id_t;

        using NodeID_t = id_t;

        using Time_t = clock_type::time_point;

        using MissingTx = MissingTx;



        struct Tx_t
        {
            id_t id;

            auto getID() const
            {
                return id;
            }

            bool operator<(Tx_t const & o) const
            {
                return id < o.id;
            }
        };


        using TxID_t = id_t;

        struct TxSet_t;

        struct MutableTxSet_t
        {
            MutableTxSet_t(TxSet_t const &);

            std::set<TxID_t> txs;

            bool insert(Tx_t t)
            {
                return txs.insert(t.id).second;
            }

            bool remove(TxID_t t)
            {
                return txs.erase(t);
            }

        };

        using TxSetID_t = id_t;
        struct TxSet_t
        {
            using mutable_t = MutableTxSet_t;

            TxSet_t() = default;

            std::set<TxID_t> txs;

            TxSet_t(mutable_t const & s) :
                txs{ s.txs }
            {

            }
            // no set?
            operator bool() const
            {
                return true;
            }

            bool hasEntry(TxID_t const id) const
            {
                return true;
            }


            boost::optional <Tx_t const>
            getEntry(TxID_t const& ) const
            {
                return boost::none;
            }
            // hash of set?
            TxSetID_t getID() const
            {
                return TxSetID_t{};
            }

            std::map<TxID_t, bool>
                getDifferences(TxSet_t const& other) const
            {
                return std::map<TxID_t, bool>{};
            }
        };



        class Ledger_t
        {
            std::set<Tx_t> txs_;

            std::uint32_t seq_ = 0;

            LgrID_t id_ = 0;


            NetClock::duration closeTimeResolution_
                = ledgerDefaultTimeResolution;

            bool closeTimeAgree = true;

        public:

            operator bool() const
            {
                return id_ != -1;
            }

            auto ID() const
            {
                return id_;
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
                return closeTimeAgree;
            }

            Time_t closeTime() const
            {
                return Time_t{};
            }

            Time_t parentCloseTime() const
            {
                return Time_t{};
            }

            LgrID_t parentID() const
            {
                return LgrID_t{};
            }


            Json::Value getJson()
            {
                return Json::Value{};
            }

        };



        struct RetryTxSet_t
        {
            RetryTxSet_t(TxSetID_t i) : id{ i } {}
            TxSetID_t id;

            void insert(Tx_t const & tx)
            {
                ts.txs.insert(tx.id);
            }

            TxSet_t ts;
        };

        class Pos_t
        {
            id_t nodeID_;

        public:
            auto getNodeID() const
            {
                return nodeID_;
            }

            LgrID_t getPrevLedger() const
            {
                return LgrID_t{};
            }

            LgrID_t getPosition() const
            {
                return LgrID_t{};
            }

            std::uint32_t getSequence() const
            {
                return 0;
            }

            bool isInitial() const
            {
                return false;

            }
            bool isBowOut() const
            {
                return false;
            }

            Time_t getCloseTime() const
            {
                return Time_t{};
            }

            void bowOut(Time_t t)
            {

            }

            bool isStale (Time_t lastValid) const
            {
                return false;
            }

            bool changePosition(
                LgrID_t const& position,
                Time_t closeTime,
                Time_t now)
            {
                return true;
            }

            Json::Value getJson()
            {
                Json::Value foo;
                foo["Position"] = 0;
                return foo;
            }
        };


        struct Callback_t
        {
            std::map<std::string, beast::Journal> j;

            beast::Journal journal(std::string const & s)
            {
                return j[s];
            }

            void startRound(Ledger_t const &)
            {
                // CHeck that this was called?
            }

            std::pair<bool, bool> getMode(const bool correctLCL) const
            {
                if (!correctLCL)
                    return{ false, false };
                return{ true, true };
            }


            Ledger_t acquireLedger(LgrID_t ledgerHash)
            {
                return Ledger_t{};
            }

            // Should be get and share?
            // If f returns true, that means it was
            // a useful proposal and should be shared
            template <class F>
            void getProposals(LgrID_t ledgerHash, F && f)
            {

            }

            // Aquire the details of the transaction corresponding
            // to this position; if not available locally, spawns
            // a networko request that will call gotMap
            TxSet_t getTxSet(Pos_t const & position)
            {
                return TxSet_t{};
            }


            bool hasOpenTransactions() const
            {
                return false;
            }

            int numProposersValidated(LgrID_t const prevLedger) const
            {
                return 0;
            }

            int numProposersFinished(LgrID_t const prevLedger) const
            {
                return 0;
            }

            Time_t getLastCloseTime() const
            {
                return Time_t{};
            }

            void setLastCloseTime(Time_t)
            {

            }

            void statusChange(ConsensusChange c, LgrID_t prevLedger,
                bool haveCorrectLCL)
            {

            }


            // don't really offload
            template <class F>
            void offloadAccept(F && f)
            {
                int dummy = 1;
                f(dummy);
            }

            void shareSet(TxSet_t const &)
            {

            }

            LgrID_t getLCL(LgrID_t prevLedger,
                LgrID_t prevParent,
                bool haveCorrectLCL)
            {
                return LgrID_t{};
            }

            void propose(Pos_t pos)
            {

            }

            Ledger_t accept(Ledger_t const &prevLedger,
                TxSet_t const & txs,
                Time_t closeTime, bool closeTimeCorrect,
                typename Time_t::duration closeResolution,
                Time_t now, std::chrono::milliseconds roundTime, RetryTxSet_t & retries)
            {
                return Ledger_t{};
            }

            bool shouldValidate(Ledger_t ledger)
            {
                return false;
            }

            void validate(Ledger_t const& ledger, Time_t now,
                bool proposing) {}

            void consensusBuilt(
                Ledger_t const & ledger,
                Json::Value && json
            ) {}

            void createOpenLedger(Ledger_t const &ledger,
                RetryTxSet_t const & retries,
                bool anyDisputes) {}

            void switchLCL(Ledger_t const &)
            {

            }

            void relayDisputedTx(Tx_t const &)
            {

            }

            void adjustCloseTime(Time_t::duration t) {}

            void endConsensus(bool correct) {}

            std::pair <TxSet_t, Pos_t>
                makeInitialPosition(
                    Ledger_t const & prevLedger,
                    bool isProposing,
                    bool isCorrectLCL,
                    Time_t closeTime,
                    Time_t now)
            {
                return std::pair <TxSet_t, Pos_t>{};
            }

        };

    };

    using Consensus = LedgerConsensus<Traits>;


    void
    testDefaultState()
    {
        using Time_t = Traits::Time_t;
        Traits::Callback_t callbacks;
        Consensus c{ callbacks, 0 };

        BEAST_EXPECT(!c.isProposing());
        BEAST_EXPECT(!c.isValidating());
        BEAST_EXPECT(!c.isCorrectLCL());
        BEAST_EXPECT(c.now() == Time_t{});
        BEAST_EXPECT(c.closeTime() == Time_t{});
        BEAST_EXPECT(c.getLastCloseProposers() == 0);
        BEAST_EXPECT(c.getLastCloseDuration() == LEDGER_IDLE_INTERVAL);
        BEAST_EXPECT(c.prevLedger().seq() == 0);
    }

    void
    testStandalone()
    {
        Traits::Callback_t callbacks;
        clock_type clock;
        Consensus c{ callbacks, 0 };
        
        Traits::Ledger_t currLedger;

        // No peers
        // Local transactions only
        // Always have ledger
        // Proposing and validating

        

        // 1. Genesis ledger

        c.startRound(clock.now(), 0, currLedger);
       
        // state -= open
        
        //send in some transactinons
         
        // transition to state closing
        c.getLCL();
        //c.gotMap(clock.now(), Traits::TxSet_t{});
        c.timerEntry(clock.now());
        // observe transition to accept
        // observe new closed ledger and it contains transactions?
        
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
        BEAST_EXPECT(1 == 2);
    }
    void
    run() override
    {
        testDefaultState();
        testStandalone();
        testPeersAgree();
        
        testPeersDisagree();

        testGetJson(); 
    }
};

LedgerConsensus_test::Traits::MutableTxSet_t::MutableTxSet_t(TxSet_t const & t)
            : txs{ t.txs }
        {

        }
BEAST_DEFINE_TESTSUITE(LedgerConsensus, consensus, ripple);
} // test
} // ripple