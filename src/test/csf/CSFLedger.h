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

#include <ripple/basics/chrono.h>
#include <test/csf/Tx.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/json/json_value.h>
#include <ripple/consensus/LedgerTiming.h>

namespace ripple {
namespace test {
namespace csf {

/** A ledger is a set of observed transactions and a sequence number
    identifying the ledger.

    Peers in the consensus process are trying to agree on a set of transactions
    to include in a ledger.  For unit testing, each transaction is a
    single integer and the ledger is a set of observed integers.  This means
    future ledgers have prior ledgers as subsets, e.g.

        Ledger 0 :  {}
        Ledger 1 :  {1,4,5}
        Ledger 2 :  {1,2,4,5,10}
        ....

    Tx - Integer
    TxSet - Set of Tx
    Ledger - Set of Tx and sequence number
*/
class Ledger
{

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
    Instance const * parentID = nullptr;

    //! Parent ledger close time
    NetClock::time_point parentCloseTime;

    //! Close time unadjusted by closeTimeResolution
    NetClock::time_point actualCloseTime;

    auto
    asTie() const
    {
        return std::tie(
            seq,
            txs,
            closeTimeResolution,
            closeTime,
            closeTimeAgree,
            parentID,
            parentCloseTime,
            actualCloseTime);
    }

    friend bool operator==(Instance const & a, Instance const & b)
    {
        return a.asTie() == b.asTie();
    }
};

static const Instance genesis;
static hash_set<Instance> instances;

public:
    
    using ID = Instance const *;

    Ledger() : instance_(&genesis) {}

    Ledger(Instance const * i): instance_{i} {}

    ID
    id() const
    {
        return instance_;
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
    actualCloseTime() const
    {
        return instance_->actualCloseTime;
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

    TxSetType const &
    txs() const
    {
        return instance_->txs;
    }

    Json::Value
        getJson() const;

    //! Apply the given transactions to this ledger
    Ledger
    close(
        TxSetType const& txs,
        NetClock::duration closeTimeResolution,
        NetClock::time_point const& consensusCloseTime,
        bool closeTimeAgree) const;

private:
    Instance const * instance_;
};


template <class Hasher>
inline void
hash_append(Hasher& h, Ledger::Instance const& instance)
{
    using beast::hash_append;
    hash_append(h, instance.asTie());
}

std::string
to_string(Ledger::ID const& id);


}  // csf
}  // test
}  // ripple

#endif
