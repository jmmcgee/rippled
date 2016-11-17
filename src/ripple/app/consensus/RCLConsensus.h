
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

#ifndef RIPPLE_APP_CONSENSUS_RCLCONSENSUS_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCONSENSUS_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/app/consensus/RCLCxCalls.h>
#include <ripple/app/consensus/RCLCxTraits.h>
#include <ripple/consensus/LedgerConsensus.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/beast/utility/Journal.h>

namespace ripple {


/** Implements the consensus process and provides inter-round state. */
class RCLConsensus
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


    std::vector <LedgerProposal>
    getStoredProposals (uint256 const& previousLedger);

   friend std::shared_ptr<LedgerConsensus<RCLCxTraits>>
    makeLedgerConsensus (
        RCLConsensus& ,
        beast::Journal journal_,
        std::unique_ptr<FeeVote> &&,
        Application& ,
        InboundTransactions& ,
        LedgerMaster& ,
        LocalTxs&,
        LedgerConsensus<RCLCxTraits>::clock_type const & clock);

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

std::shared_ptr<LedgerConsensus<RCLCxTraits>>
makeLedgerConsensus (
    RCLConsensus& consensus,
    beast::Journal journal_,
    std::unique_ptr<FeeVote> && feeVote,
    Application& app,
    InboundTransactions& inboundTransactions,
    LedgerMaster& ledgerMaster,
    LocalTxs& localTxs,
    LedgerConsensus<RCLCxTraits>::clock_type const & clock);

}

#endif
