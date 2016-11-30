//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVID_tED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================
#ifndef RIPPLE_CONSENSUS_ConsensusProposal_H_INCLUDED
#define RIPPLE_CONSENSUS_ConsensusProposal_H_INCLUDED

#include <cstdint>
#include <ripple/json/json_value.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple
{
/**
 Represents a proposed position taken during a round of consensus.
 This can either be our own proposal or a proposal from a peer.
 */
template <
    class NodeID_t,
    class LedgerID_t,
    class Position_t,
    class Time_t>
class ConsensusProposal
{
private:
    // A peer initial joins the consensus process
    static std::uint32_t const seqJoin = 0;

    // A peer wants to bow out and leave the consensus process
    static std::uint32_t const seqLeave = 0xffffffff;

public:

    using NodeID = NodeID_t;

    // Peer Proposal
    ConsensusProposal(
        LedgerID_t const& prevLedger,
        std::uint32_t seq,
        Position_t const& position,
        Time_t closeTime,
        Time_t now,
        NodeID_t const& nodeID)
    : previousLedger_(prevLedger)
    , position_(position)
    , closeTime_(closeTime)
    , time_(now)
    , proposeSeq_(seq)
    , nodeID_(nodeID)
    {

    }

    // Our proposal
    ConsensusProposal(
        LedgerID_t const& prevLgr,
        Position_t const& position,
        Time_t closeTime,
        Time_t now,
        NodeID_t const& nodeID)
    : previousLedger_(prevLgr)
    , position_(position)
    , closeTime_(closeTime)
    , time_(now)
    , proposeSeq_(seqJoin)
    , nodeID_(nodeID)
    {

    }

    // @return identifying index of which peer took this position
    NodeID_t const& getNodeID () const
    {
        return nodeID_;
    }

    // @return identify
    Position_t const& getPosition () const
    {
        return position_;
    }

    // @return the prior accepted ledger this position is based on
    LedgerID_t const& getPrevLedgerID () const
    {
        return previousLedger_;
    }

    // @return
    std::uint32_t getProposeSeq () const
    {
        return proposeSeq_;
    }

    // @return the current position on the consensus close time
    Time_t getCloseTime () const
    {
        return closeTime_;
    }

    // @return when this position was taken
    Time_t getSeenTime () const
    {
        return time_;
    }

    // @return whether this is the first position taken during the current
    // consensus round
    bool isInitial () const
    {
        return proposeSeq_ == seqJoin;
    }

    // @return whether this node left the consensus process
    bool isBowOut () const
    {
        return proposeSeq_ == seqLeave;
    }

    // @return whether this position is stale relative to the provided cutoff
    bool isStale (Time_t cutoff) const
    {
        return time_ <= cutoff;
    }

    /**
        Update the position during the consensus process. This will increment
        the proposal's sequence number.

        @param newPosition the new position taken
        @param newCloseTime the new close time
        @param now the time the new position was taken

        @return true if the position was updated or false if this node has
        already left this consensus round
     */
    bool changePosition(
        Position_t const& newPosition,
        Time_t newCloseTime,
        Time_t now)
    {
         if (proposeSeq_ == seqLeave)
            return false;

        position_    = newPosition;
        closeTime_      = newCloseTime;
        time_           = now;
        ++proposeSeq_;
        return true;
    }

    // Leave consensus
    void bowOut(Time_t now)
    {
        time_           = now;
        proposeSeq_     = seqLeave;
    }


    Json::Value getJson () const
    {
        using std::to_string;

        Json::Value ret = Json::objectValue;
        ret[jss::previous_ledger] = to_string (getPrevLedgerID());

        if (!isBowOut())
        {
            ret[jss::transaction_hash] = to_string (getPosition());
            ret[jss::propose_seq] = getProposeSeq();
        }

        ret[jss::close_time] = to_string(getCloseTime().time_since_epoch().count());

        return ret;
    }

private:

    // Unique identifier of prior ledger this proposal is based on
    LedgerID_t previousLedger_;

    // Unique identifier of the position this proposal is taking
    Position_t position_;

    // The ledger close time this position is taking
    Time_t closeTime_;

    // The time this position was last updated
    Time_t time_;

    // The sequence number of positions taken by this node during this consensus
    // round.
    std::uint32_t proposeSeq_;

    // The identifier of the node taking this position
    NodeID_t nodeID_;

};
}
#endif
