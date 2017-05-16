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
#ifndef RIPPLE_TEST_CSF_CSFLEDGER_H_INCLUDED
#define RIPPLE_TEST_CSF_CSFLEDGER_H_INCLUDED

#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/json/json_value.h>
#include <test/csf/Tx.h>

namespace ripple {
namespace test {
namespace csf {

/** A ledger is a set of observed transactions and a sequence number
    identifying the ledger.

    Peers in the consensus process are trying to agree on a set of transactions
    to include in a ledger.  For unit testing, each transaction is a
    single integer and the ledger is the set of observed integers.  This means
    future ledgers have prior ledgers as subsets, e.g.

        Ledger 0 :  {}
        Ledger 1 :  {1,4,5}
        Ledger 2 :  {1,2,4,5,10}
        ....

    Ledgers are immutable value types.  All ledgers with the same sequence
    number, transactions, close time, etc. will have the same ledger ID. Since
    the parent ledger ID is part of type, this also means distinct histories of
    ledgers will have distinct ids.

*/
class Ledger
{
    // The instance is the common immutable types that will be assigned an ID
    struct Instance
    {
        // Sequence number
        std::uint32_t seq = 0;

        // Transactions added to generate this ledger
        TxSetType txs;

        // Resolution used to determine close time
        NetClock::duration closeTimeResolution = ledgerDefaultTimeResolution;

        //! When the ledger closed (up to closeTimeResolution
        NetClock::time_point closeTime;

        //! Whether consenssus agreed on the close time
        bool closeTimeAgree = true;

        //! Parent ledger id
        std::uint32_t parentID = 0;

        //! Parent ledger close time
        NetClock::time_point parentCloseTime;

        auto
        asTie() const
        {
            return std::tie(seq, txs, closeTimeResolution, closeTime,
                closeTimeAgree, parentID, parentCloseTime);
        }

        friend bool
        operator==(Instance const& a, Instance const& b)
        {
            return a.asTie() == b.asTie();
        }

        template <class Hasher>
        friend void
        hash_append(Hasher& h, Ledger::Instance const& instance)
        {
            using beast::hash_append;
            hash_append(h, instance.asTie());
        }
    };

    // These static members implement a flyweight style management of ledgers
    // for the entire application lifetime.  They are not currently thread safe.

    // Single genesis instance
    static const Instance genesis;
    // Set of all known post-genesis ledgers; note this is never pruned
    static hash_map<Instance, std::uint32_t> instances;
    // Id to assign to the next unique ledger instance
    static std::uint32_t nextUniqueID;

    Ledger(std::uint32_t id, Instance const* i) : id_{id}, instance_{i}
    {
    }

public:
    using ID = std::uint32_t;

    Ledger() : id_{0}, instance_(&genesis)
    {
    }

    ID
    id() const
    {
        return id_;
    }

    std::uint32_t
    seq() const
    {
        return instance_->seq;
    }

    NetClock::duration
    closeTimeResolution() const
    {
        return instance_->closeTimeResolution;
    }

    bool
    closeAgree() const
    {
        return instance_->closeTimeAgree;
    }

    NetClock::time_point
    closeTime() const
    {
        return instance_->closeTime;
    }

    NetClock::time_point
    parentCloseTime() const
    {
        return instance_->parentCloseTime;
    }

    ID
    parentID() const
    {
        return instance_->parentID;
    }

    TxSetType const&
    txs() const
    {
        return instance_->txs;
    }

    Json::Value getJson() const;

    //! Apply the given transactions to this ledger
    Ledger close(TxSetType const& txs,
        NetClock::duration closeTimeResolution,
        NetClock::time_point const& consensusCloseTime,
        bool closeTimeAgree) const;

private:
    ID id_;
    Instance const* instance_;
};


}  // csf
}  // test
}  // ripple

#endif
