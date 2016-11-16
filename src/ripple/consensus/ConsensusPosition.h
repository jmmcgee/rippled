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
#ifndef RIPPLE_CONSENSUS_CONSENSUSPOSITION_H_INCLUDED
#define RIPPLE_CONSENSUS_CONSENSUSPOSITION_H_INCLUDED

#include <cstdint>
#include <ripple/json/json_value.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple
{
/**
 Represents a proposed position taken during a round of consensus.
 This can either be our own position or one of our peers.
 */
template <
    class NodeID_t,
    class LgrID_t,
    class PosID_t,
    class Time_t>
class ConsensusPosition
{
private:
    // A peer initial joins the consensus process
    static std::uint32_t const seqJoin = 0;

    // A peer wants to bow out and leave the consensus process
    static std::uint32_t const seqLeave = 0xffffffff;

public:
    // Peer Proposal
    ConsensusPosition(
        LgrID_t const& prevLedger,
        std::uint32_t seq,
        PosID_t const& position,
        Time_t closeTime,
        Time_t now,
        NodeID_t const& nodeID_t)
    : mPreviousLedger(prevLedger)
    , mCurrentHash(position)
    , mCloseTime(closeTime)
    , mTime(now)
    , mProposeSeq(seq)
    , mPeerID(nodeID_t)
    {

    }

    // Our proposal
    ConsensusPosition(
        LgrID_t const& prevLgr,
        PosID_t const& position,
        Time_t closeTime,
        Time_t now)
    : mPreviousLedger(prevLgr)
    , mCurrentHash(position)
    , mCloseTime(closeTime)
    , mProposeSeq(seqJoin)
    , mTime(now)
    {

    }

    // @return identifying index of which peer took this position
    NodeID_t const& getPeerID () const
    {
        return mPeerID;
    }

    // @return identify
    PosID_t const& getPosition () const
    {
        return mCurrentHash;
    }

    // @return the prior accepted ledger this position is based on
    LgrID_t const& getPrevLedger () const
    {
        return mPreviousLedger;
    }

    // @return
    std::uint32_t getProposeSeq () const
    {
        return mProposeSeq;
    }

    // @return the current position on the consensus close time
    Time_t getCloseTime () const
    {
        return mCloseTime;
    }

    // @return when this position was taken
    Time_t getSeenTime () const
    {
        return mTime;
    }

    // @return whether this is the first position taken during the current
    // consensus round
    bool isInitial () const
    {
        return mProposeSeq == seqJoin;
    }

    // @return whether this node left the consensus process
    bool isBowOut () const
    {
        return mProposeSeq == seqLeave;
    }

    // @return whether this position is stale relative to the provided cutoff
    bool isStale (NetClock::time_point cutoff) const
    {
        return mTime <= cutoff;
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
        PosID_t const& newPosition,
        Time_t newCloseTime,
        Time_t now)
    {
         if (mProposeSeq == seqLeave)
            return false;

        mCurrentHash    = newPosition;
        mCloseTime      = newCloseTime;
        mTime           = now;
        ++mProposeSeq;
        return true;
    }

    // Leave consensus
    void bowOut(Time_t now)
    {
        mTime           = now;
        mProposeSeq     = seqLeave;
    }


    Json::Value getJson () const
    {
        Json::Value ret = Json::objectValue;
        ret[jss::previous_ledger] = to_string (getPrevLedger());

        if (!isBowOut())
        {
            ret[jss::transaction_hash] = to_string (getPosition());
            ret[jss::propose_seq] = getProposeSeq();
        }

        ret[jss::close_time] = getCloseTime().time_since_epoch().count();

        return ret;
    }

private:

    // Unique identifier of prior ledger this proposal is based on
    LgrID_t mPreviousLedger;

    // Unique identifier of the position this proposal is taking
    PosID_t mCurrentHash;

    // The ledger close time this position is taking
    Time_t mCloseTime;

    // The time this position was last updated
    Time_t mTime;

    // The sequence number of positions taken by this node during this consensus
    // round.
    std::uint32_t mProposeSeq;

    // The identifier of the node taking this position
    NodeID_t mPeerID;

};
}
#endif
