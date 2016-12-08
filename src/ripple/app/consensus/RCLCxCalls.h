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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXCALLS_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXCALLS_H_INCLUDED

#include <ripple/protocol/UintTypes.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/consensus/ConsensusTypes.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/app/consensus/RCLCxTx.h>
#include <ripple/consensus/DisputedTx.h>

namespace ripple {

class RCLCxConsensus;
class InboundTransactions;
class LocalTxs;
class RCLCxLedger;


class RCLCxCalls
{
public:
    RCLCxCalls (
        Application&,
        RCLCxConsensus&,
        std::unique_ptr<FeeVote>&&,
        LedgerMaster&,
        LocalTxs &,
        InboundTransactions &,
        beast::Journal);

    uint256
    getLCL (
        uint256 const& currentLedger,
        uint256 const& priorLedger,
        bool believedCorrect);

    std::pair <bool, bool>
    getMode (bool correctLCL);

    void
    share (RCLTxSet const& set);

    void
    propose (LedgerProposal const& position);

    std::vector<LedgerProposal>
    proposals (LedgerHash const& prevLedger);

    std::pair <RCLTxSet, LedgerProposal>
    makeInitialPosition (
        RCLCxLedger const & prevLedger,
        bool isProposing,
        bool isCorrectLCL,
        NetClock::time_point closeTime,
        NetClock::time_point now);

    boost::optional<RCLCxLedger>
    acquireLedger(LedgerHash const & ledgerHash);

   /*
    * Senda status change message to peers due to a change in ledger
    * @param c the reason for the change
    * @param ledger the ledger we are changing to
    * @param haveCorrectLCL whether we believe this is the correct LCL
    */
    void
    statusChange(
        ConsensusChange c,
        RCLCxLedger const & ledger,
        bool haveCorrectLCL);

    void
    accept(
        RCLTxSet const& set,
        NetClock::time_point consensusCloseTime,
        bool proposing_,
        bool & validating_,
        bool haveCorrectLCL_,
        bool consensusFail_,
        LedgerHash const &prevLedgerHash_,
        RCLCxLedger const & previousLedger_,
        NetClock::duration closeResolution_,
        NetClock::time_point const & now,
        std::chrono::milliseconds const & roundTime_,
        hash_map<RCLCxTx::ID, DisputedTx <RCLCxTx, NodeID>> const & disputes_,
        std::map <NetClock::time_point, int> closeTimes_,
        NetClock::time_point const & closeTime,
        Json::Value && json
    );



    /*
    * Signal the end of consensus to the application, which will start the
    * next round.
    */
    void
    endConsensus(bool correctLCL);

    /*
    * @return a handle to the given journal
    */
    beast::Journal
    journal(std::string const & s) const;

    /*
    *@return whether the open ledger has any transactions
    */
    bool
    hasOpenTransactions() const;

    /*
    * @return the number of proposers that validated the last validated ledger
    */
    int
    numProposersValidated(LedgerHash const & h) const;

    /*
    * @return the number of validating peers that have validated a ledger
    * succeeding the one provided
    */
    int
    numProposersFinished(LedgerHash const & h) const;

    /**
    * If the provided transaction hasn't been shared recently, relay it to peers
    * @param tx the disputed transaction to relay
    */
    void
    relay(DisputedTx <RCLCxTx, NodeID> const & dispute);

    /**
     * Relay the given proposal to all peers
     */
    void
    relay(LedgerProposal const & proposal);

    /*
    * Schedule an offloaded call to accept
    */
    void
    dispatchAccept(JobQueue::JobFunction const & f);

    /**
    * @return the transaction set associated with this position
    */
    boost::optional<RCLTxSet>
    acquireTxSet(LedgerProposal const & position);

private:

    /*
    * Accept the given the provided set of consensus transactions and build
    * the last closed ledger. Since consensus just agrees on which transactions
    * to apply, but not whether they make it into the closed ledger, this
    * function also populates retriableTxs with those that can be retried in the
    * next round.
    * @return the newly built ledger
    */
    RCLCxLedger
    accept(
        RCLCxLedger const & previousLedger,
        RCLTxSet const & set,
        NetClock::time_point closeTime,
        bool closeTimeCorrect,
        NetClock::duration closeResolution,
        NetClock::time_point now,
        std::chrono::milliseconds roundTime,
        CanonicalTXSet & retriableTxs
    );

    /*
    * Validate the given ledger and share with peers as necessary
    * @param ledger the ledger to validate
    * @param now current time
    * @param proposing whether we were proposing transactions while generating
    * this ledger.  If we are not proposing, this message is to inform our peers
    * that we know we aren't fully participating in consensus.
    */
    void validate(
        RCLCxLedger const & ledger,
        NetClock::time_point now,
        bool proposing);

    /*
    * Create the new open ledger based on the prior closed ledger and any
    * retriable transactions
    * @param closedLedger the ledger just closed that is the starting point for
    * the open ledger
    * @param retriableTxs the set of transactions to attempt to retry in the
    * newly opened ledger
    * @param anyDisputes whether any of the retriableTxs were disputed by us
    * during consensus
    */
    void createOpenLedger(
        RCLCxLedger const & closedLedger,
        CanonicalTXSet & retriableTxs,
        bool anyDisputes);


    Application& app_;
    RCLCxConsensus& consensus_;
    std::unique_ptr <FeeVote> feeVote_;
    LedgerMaster & ledgerMaster_;
    LocalTxs & localTxs_;
    InboundTransactions& inboundTransactions_;
    beast::Journal j_;

    NodeID nodeID_;
    PublicKey valPublic_;
    SecretKey valSecret_;
    LedgerHash acquiringLedger_;

    // The timestamp of the last validation we used, in network time. This is
    // only used for our own validations.
    NetClock::time_point lastValidationTime_;

};

} // namespace ripple
#endif
