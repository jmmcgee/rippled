
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_LEDGER_IMPL_CONSENSUSIMP_H_INCLUDED
#define RIPPLE_APP_LEDGER_IMPL_CONSENSUSIMP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/app/consensus/RCLCxCalls.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/beast/utility/Journal.h>

namespace ripple {

class RCLCxTraits;
template <class T> class LedgerConsensusImp;

/** Implements the consensus process and provides inter-round state. */
class ConsensusImp
{
public:

    using Proposals = hash_map <NodeID, std::deque<LedgerProposal::pointer>>;

    bool
    isProposing () const;

    bool
    isValidating () const;

    void
    setLastCloseTime(NetClock::time_point t)
    {
        callbacks_->setLastCloseTime(t);
    }

    NetClock::time_point
    getLastCloseTime() const
    {
        return callbacks_->getLastCloseTime();
    }


    void
    storeProposal (
        LedgerProposal::ref proposal,
        NodeID const& nodeID);

    void
    setProposing (bool p, bool v);

    NetClock::time_point
    validationTimestamp (NetClock::time_point vt);


    std::vector <RCLCxPos>
    getStoredProposals (uint256 const& previousLedger);

   friend std::shared_ptr<LedgerConsensusImp<RCLCxTraits>>
    makeLedgerConsensus (
        ConsensusImp& ,
        beast::Journal journal_,
        std::unique_ptr<FeeVote> &&,
        Application& ,
        InboundTransactions& ,
        LedgerMaster& ,
        LocalTxs& );

private:
    std::unique_ptr <RCLCxCalls> callbacks_;

    bool proposing_ = false;
    bool validating_ = false;

    // The timestamp of the last validation we used, in network time. This is
    // only used for our own validations.
    NetClock::time_point lastValidationTimestamp_;

    Proposals storedProposals_;

    // lock to protect storedProposals_
    std::mutex lock_;
};

std::shared_ptr<LedgerConsensusImp<RCLCxTraits>>
makeLedgerConsensus (
    ConsensusImp& consensus,
    beast::Journal journal_,
    std::unique_ptr<FeeVote> && feeVote,
    Application& app,
    InboundTransactions& inboundTransactions,
    LedgerMaster& ledgerMaster,
    LocalTxs& localTxs);

}

#endif
