//------------------------------------------------------------------------------
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

#include <ripple/app/consensus/RCLCxLedger.h>
#include <ripple/app/consensus/RCLCxPeerPos.h>
#include <ripple/app/consensus/RCLCxTx.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <memory>

namespace ripple {

class Application;
class FeeVote;
class InboundTransactions;
class LocalTxs;
class LedgerMaster;

/** Manges the generic consensus algorithm for use by the RCL.

*/
class RCLConsensus
{
    struct Impl;

public:
    //! Constructor
    RCLConsensus(
        Application& app,
        std::unique_ptr<FeeVote>&& feeVote,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs,
        InboundTransactions& inboundTransactions,
        beast::abstract_clock<std::chrono::steady_clock> const& clock,
        beast::Journal journal);

    RCLConsensus(RCLConsensus const&) = delete;

    RCLConsensus&
    operator=(RCLConsensus const&) = delete;

    ~RCLConsensus();

    //! Whether we are validating consensus ledgers.
    bool
    validating() const;

    bool
    haveCorrectLCL() const;

    bool
    proposing() const;

    /** Get the Json state of the consensus process.

        Called by the consensus_info RPC.

        @param full True if verbose response desired.
        @return     The Json state.
    */
    Json::Value
    getJson(bool full) const;

    //! See Consensus::startRound
    void
    startRound(
        NetClock::time_point const& now,
        RCLCxLedger::ID const& prevLgrId,
        RCLCxLedger const& prevLgr);

    //! See Consensus::timerEntry
    void
    timerEntry(NetClock::time_point const& now);

    //! See Consensus::gotTxSet
    void
    gotTxSet(NetClock::time_point const& now, RCLTxSet const& txSet);

    /** Returns validation public key */
    PublicKey const&
    getValidationPublicKey() const;

    /** Set validation private and public key pair. */
    void
    setValidationKeys(SecretKey const& valSecret, PublicKey const& valPublic);

    RCLCxLedger::ID
    prevLedgerID() const;

    //! Get the number of proposing peers that participated in the previous
    //! round.
    std::size_t
    prevProposers() const;

    /** Get duration of the previous round.

        The duration of the round is the establish phase, measured from closing
        the open ledger to accepting the consensus result.

        @return Last round duration in milliseconds
    */
    std::chrono::milliseconds
    prevRoundTime() const;

    void
    simulate(
        NetClock::time_point const& now,
        boost::optional<std::chrono::milliseconds> consensusDelay);

    bool
    peerProposal(
        NetClock::time_point const& now,
        RCLCxPeerPos const& newProposal);

private:
    std::unique_ptr<Impl> impl_;
};

}

#endif
