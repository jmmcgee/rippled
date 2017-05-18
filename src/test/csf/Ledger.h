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
#ifndef RIPPLE_TEST_CSF_LEDGER_H_INCLUDED
#define RIPPLE_TEST_CSF_LEDGER_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <test/csf/Tx.h>
#include <ripple/basics/tagged_integer.h>
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
public:
    struct SeqTag;
    using Seq = ripple::tagged_integer<std::uint32_t, SeqTag>;

    struct IdTag;
    using ID = ripple::tagged_integer<std::uint32_t, IdTag>;

    ID const
    id() const
    {
        return id_;
    }

    Seq const
    seq() const
    {
        return seq_;
    }

    NetClock::duration
    closeTimeResolution() const
    {
        return closeTimeResolution_;
    }

    bool
    closeAgree() const
    {
        return closeTimeAgree_;
    }

    NetClock::time_point
    closeTime() const
    {
        return closeTime_;
    }

    NetClock::time_point
    parentCloseTime() const
    {
        return parentCloseTime_;
    }

    ID
    parentID() const
    {
        return parentID_;
    }

    TxSetType const &
    txs() const
    {
        return txs_;
    }

    Json::Value
    getJson() const
    {
        Json::Value res(Json::objectValue);
        res["seq"] = static_cast<Seq::value_type>(seq());
        return res;
    }

    //! Apply the given transactions to this ledger
    Ledger
    close(
        TxSetType const& txs,
        NetClock::duration closeTimeResolution,
        NetClock::time_point const& consensusCloseTime,
        bool closeTimeAgree) const
    {
        Ledger res{*this};
        res.txs_.insert(txs.begin(), txs.end());
        res.seq_ = seq() + 1;
        res.closeTimeResolution_ = closeTimeResolution;
        res.actualCloseTime_ = consensusCloseTime;
        res.closeTime_ = effCloseTime(
            consensusCloseTime, closeTimeResolution, parentCloseTime_);
        res.closeTimeAgree_ = closeTimeAgree;
        res.parentCloseTime_ = closeTime();
        res.parentID_ = id();
        return res;
    }

private:
    //! Unique identifier of ledger
    ID id_;

    //! Sequence number that is one more than the parent ledger's sequence num
    Seq seq_;

    //! Transactions in this ledger
    TxSetType txs_;

    //! Bucket resolution used to determine close time
    NetClock::duration closeTimeResolution_ = ledgerDefaultTimeResolution;

    //! When the ledger closed
    NetClock::time_point closeTime_;

    //! Whether consenssus agreed on the close time
    bool closeTimeAgree_ = true;

    //! Parent ledger id
    ID parentID_;

    //! Parent ledger close time
    NetClock::time_point parentCloseTime_;

    //! Close time unadjusted by closeTimeResolution
    NetClock::time_point actualCloseTime_;
};

}  // csf
}  // test
}  // ripple

#endif
