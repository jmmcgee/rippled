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

namespace ripple {

class ConsensusImp;
class LocalTxs;
class RCLTxSet;
class RCLCxPos;
class RCLCxLedger;
class RCLCxRetryTxSet;

class RCLCxCalls
{
public:

    RCLCxCalls (
        Application&,
        ConsensusImp&,
        FeeVote&,
        LedgerMaster&,
        LocalTxs &,
        beast::Journal&);

    uint256 getLCL (
        uint256 const& currentLedger,
        uint256 const& priorLedger,
        bool believedCorrect);

    std::pair <bool, bool> getMode (bool correctLCL);

    void shareSet (RCLTxSet const& set);

    void propose (RCLCxPos const& position);

    void getProposals (LedgerHash const& prevLedger,
        std::function <bool (RCLCxPos const&)>);

    std::pair <RCLTxSet, RCLCxPos>
    makeInitialPosition (
        RCLCxLedger const & prevLedger,
        bool isProposing,
        bool isCorrectLCL,
        NetClock::time_point closeTime,
        NetClock::time_point now);

    RCLCxLedger acquireLedger(LedgerHash const & ledgerHash);

    enum class ChangeType {Closing, Accepted};
    /*
    * Senda status change message to peers due to a change in ledger
    * @param c the reason for the change
    * @param ledger the ledger we are changing to
    * @param haveCorrectLCL whether we believe this is the correct LCL
    */
    void statusChange(
        ChangeType c,
        RCLCxLedger const & ledger,
        bool haveCorrectLCL);

    /*
    * Build the last closed ledger given the provided sset of consensus
    * transactions. Since consensus just agrees on which transactions to apply,
    * but not whether they make it into the closed ledger, this function also
    * populates retriableTxs with those that can be retried in the next round.
    *
    * @return the newly built ledger
    */
    RCLCxLedger buildLastClosedLedger(
        RCLCxLedger const & previousLedger,
        RCLTxSet const & set,
        NetClock::time_point closeTime,
        bool closeTimeCorrect,
        NetClock::duration closeResolution,
        NetClock::time_point now,
        std::chrono::milliseconds roundTime,
        RCLCxRetryTxSet & retriableTxs
    );

    /*
    * @return whether the newly created ledger should be validated during
    * the accept phase of consensus
    */
    bool shouldValidate(RCLCxLedger const & ledger);

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
    * Notify that consensus has built the given ledger and see if it can be
    * acccepted as fullyl validated
    */
    void consensusBuilt(
        RCLCxLedger const & ledger,
        Json::Value && json
    );

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
        RCLCxRetryTxSet & retriableTxs,
        bool anyDisputes);

    /*
    * Switch the local last closed ledger.
    */
    void switchLCL(RCLCxLedger const & ledger);

    /*
    * Adjust closed time based on observed offset to peers during last round
    */
    void adjustCloseTime(std::chrono::duration<std::int32_t> offset);


    /*
    * Signal the end of consensus to the application, which will start the
    * next round.
    */
    void endConsensus(bool correctLCL);



private:

    Application& app_;
    ConsensusImp& consensus_;
    FeeVote& feeVote_;
    LedgerMaster & ledgerMaster_;
    LocalTxs & localTxs_;
    beast::Journal j_;

    PublicKey valPublic_;
    SecretKey valSecret_;
    LedgerHash acquiringLedger_;


};

} // namespace ripple
#endif
