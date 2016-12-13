
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

#ifndef RIPPLE_APP_CONSENSUS_RCLCxConsensus_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCxConsensus_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/consensus/DisputedTx.h>
#include <ripple/app/consensus/RCLCxLedger.h>
#include <ripple/app/consensus/RCLCxTx.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/core/JobQueue.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/overlay/Message.h>
namespace ripple {

class InboundTransactions;
class LocalTxs;
class LedgerMaster;

//! Types used to adapt consensus for RCL
struct RCLCxTraits
{
	using NetTime_t = NetClock::time_point;
	using Ledger_t = RCLCxLedger;
	using Proposal_t = LedgerProposal;
	using TxSet_t = RCLTxSet;
	using MissingTxException_t = SHAMapMissingNode;
};

/**
    Adapts the generic Consensus algorithm for use by RCL.

	@note The enabled_shared_from_this base is used to allow accepting
	the ledger in a worker thread.
*/
class RCLCxConsensus : public Consensus<RCLCxConsensus, RCLCxTraits>
	                 , public std::enable_shared_from_this <RCLCxConsensus>
	                 , public CountedObject <RCLCxConsensus>
{
public:
	using Base = Consensus<RCLCxConsensus, RCLCxTraits>;
	using Base::accept;

	RCLCxConsensus(
        Application& app,
        std::unique_ptr<FeeVote> && feeVote,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs,
        InboundTransactions& inboundTransactions,
		typename Base::clock_type const & clock,
        beast::Journal journal);

	static char const* getCountedObjectName() { return "Consensus"; }

	/**
	    Save the given consensus proposed by a peer with nodeID for later
		use in consensus.

		@param proposal Proposed peer position
		@param nodeID ID of peer
	*/
	void
	storeProposal( LedgerProposal::ref proposal, NodeID const& nodeID);

private:
	friend class Consensus<RCLCxConsensus, RCLCxTraits>;

	//--------------------------------------------------------------------------
    // Consensus type and callback requirements

	/**
	    Get the last closed ledger (LCL) seen on the network
		@param currentLedger Hash of current ledger used in consensus
		@param priorLedger Hash of prior ledger used in consensus
		@param believedCorrect Whether consensus believes currentLedger is true LCL

		@return The hash of the last closed network
	 */
    uint256
    getLCL (
        uint256 const& currentLedger,
        uint256 const& priorLedger,
        bool believedCorrect);

	//! @return Whether consensus should be (proposing, validating)
    std::pair <bool, bool>
    getMode ();

	//! Share the given tx set with peers
    void
    share (RCLTxSet const& set);

	//! Propose the given position to my peers
    void
    propose (LedgerProposal const& position);

	/**
		@param prevLedger The base ledger which proposals are based on
	    @return The set of proposals
	*/
    std::vector<LedgerProposal>
    proposals (LedgerHash const& prevLedger);

	/**
	    Create our initial position of transactions to accept in this round
		of consensus.

		@param prevLedger The ledger the transactions apply to
		@param isProposing Whether we are currently proposing
		@param isCorrectLCL Whether we have the correct LCL
		@param closeTime When we believe the ledger closed
		@param now The current network adjusted time

		@return Pair of (i)  transactions we believe are in the ledger
		                (ii) the corresponding proposal of those transactions
						     to send to peers
	 */
    std::pair <RCLTxSet, LedgerProposal>
    makeInitialPosition (
        RCLCxLedger const & prevLedger,
        bool isProposing,
        bool isCorrectLCL,
        NetClock::time_point closeTime,
        NetClock::time_point now);

	/**
	    Attempt to acquire a specific ledger.  If not available, asynchronously
		seeks to acquire from the network

		@param ledgerHash The hash of the ledger acquire

		@return Optional ledger, will be seated if we locally had the ledger
	 */
    boost::optional<RCLCxLedger>
    acquireLedger(LedgerHash const & ledgerHash);

   /**
       Notification that the ledger has closed.

       @param ledger the ledger we are changing to
       @param haveCorrectLCL whether we believe this is the correct LCL
    */
	void
	onClose(RCLCxLedger const & ledger, bool haveCorrectLCL);

	/**
	    Notification that a new consensus round has begun.

		@param ledger The ledger we are building consensus on
	*/
	void
	onStartRound(RCLCxLedger const & ledger);

	/**
	    Accept a new ledger based on the given transactions
		TODO: The arguments need to be simplified!
		@return Whether we should continue validating
	 */
    bool
    accept(
        RCLTxSet const& set,
        NetClock::time_point consensusCloseTime,
        bool proposing_,
        bool validating_,
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

    /**
	    Signal the end of consensus to the application, which will start the
        next round.

		@param correctLCL Whether we believe we have the correct LCL
    */
    void
    endConsensus(bool correctLCL);

    /**
        @return whether the open ledger has any transactions
    */
    bool
    hasOpenTransactions() const;

    /**
	    @param h The hash of the ledger of interest
        @return the number of proposers that validated a ledger
    */
    int
    numProposersValidated(LedgerHash const & h) const;

    /**
	    @param h The hash of the ledger of interest.
        @return The number of validating peers that have validated a ledger
                succeeding the one provided.
    */
    int
    numProposersFinished(LedgerHash const & h) const;

    /**
        If the provided transaction hasn't been shared recently, relay it to peers

	    @param tx The disputed transaction to relay.
    */
    void
    relay(DisputedTx <RCLCxTx, NodeID> const & dispute);

    /**
        Relay the given proposal to all peers
		@param proposal The proposal to relay.
     */
    void
    relay(LedgerProposal const & proposal);

    /**
	    @brief Dispatch a call to accept

		Accepting a ledger may be expensive, so this function can dispatch
		that call to another thread if desired and must call the accept
		method of the generic consensus algorithm.

		@param txSet The transactions to accept.
    */
    void
    dispatchAccept(RCLTxSet const & txSet);

    /**
	    Acquire the transaction set associated with a proposal.  If the
		transaction set is not available locally, will attempt acquire it
		from the network.

		@param position The proposal to acquire transactions for
		@return Optional set of transactions, seated if available.
    */
    boost::optional<RCLTxSet>
    acquireTxSet(LedgerProposal const & position);

    //-------------------------------------------------------------------------
	// Additional helpers

    /**
        Accept the given the provided set of consensus transactions and build
        the last closed ledger. Since consensus just agrees on which transactions
        to apply, but not whether they make it into the closed ledger, this
        function also populates retriableTxs with those that can be retried in the
        next round.

	   @return The newly built ledger
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

    /**
       Validate the given ledger and share with peers as necessary

	   @param ledger the ledger to validate
       @param now current time
       @param proposing whether we were proposing transactions while generating
       this ledger.  If we are not proposing, this message is to inform our peers
       that we know we aren't fully participating in consensus.
    */
    void validate(
        RCLCxLedger const & ledger,
        NetClock::time_point now,
        bool proposing);

    /**
        Create the new open ledger based on the prior closed ledger and any
        retriable transactions
        @param closedLedger the ledger just closed that is the starting point for
        the open ledger
        @param retriableTxs the set of transactions to attempt to retry in the
        newly opened ledger
        @param anyDisputes whether any of the retriableTxs were disputed by us
        during consensus
    */
    void createOpenLedger(
        RCLCxLedger const & closedLedger,
        CanonicalTXSet & retriableTxs,
        bool anyDisputes);

	/**
	    Notify peers of a consensus state change
	    @param ne Event type for notification
		@param ledger The ledger at the time of the state change
		@param haveCorrectLCL Whether we believ we have the correct LCL.
	*/
	void
	notify(protocol::NodeEvent ne, RCLCxLedger const & ledger, bool haveCorrectLCL);


    Application& app_;
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

    using Proposals = hash_map <NodeID, std::deque<LedgerProposal::pointer>>;
    Proposals proposals_;
    std::mutex proposalsLock_;

};

}

#endif
