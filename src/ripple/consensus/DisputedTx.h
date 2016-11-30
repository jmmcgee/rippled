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

#ifndef RIPPLE_APP_CONSENSUS_DISPUTEDTX_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_DISPUTEDTX_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/json/json_value.h>

namespace ripple {

/** A tx_ discovered to be in dispute during conensus.

    During consensus, a @ref DisputedTx is created when a tx_
    is discovered to be disputed. The object persists only as long as
    the dispute.

    Undisputed transactions have no corresponding @ref DisputedTx object.
*/

template <class Tx_t, class NodeID_t>
class DisputedTx
{
public:
    using TxID_t   = typename Tx_t::ID;

    DisputedTx (Tx_t const& tx,
            bool ourVote, beast::Journal j)
        : yays_ (0)
        , nays_ (0)
        , ourVote_ (ourVote)
        , tx_ (tx)
        , j_ (j)
    {
    }

    TxID_t const& ID () const
    {
        return tx_.id();
    }

    bool getOurVote () const
    {
        return ourVote_;
    }

    Tx_t const& tx () const
    {
        return tx_;
    }

    void setOurVote (bool o)
    {
        ourVote_ = o;
    }

    void setVote (NodeID_t const& peer, bool votesYes);
    void unVote (NodeID_t const& peer);

    bool updateVote (int percentTime, bool proposing);
    Json::Value getJson () const;

private:
    int yays_;
    int nays_;
    bool ourVote_;
    Tx_t tx_;

    hash_map <NodeID_t, bool> votes_;
    beast::Journal j_;
};

// Track a peer's yes/no vote on a particular disputed tx_
template <class Tx_t, class NodeID_t>
void DisputedTx<Tx_t, NodeID_t>::setVote (NodeID_t const& peer, bool votesYes)
{
    auto res = votes_.insert (std::make_pair (peer, votesYes));

    // new vote
    if (res.second)
    {
        if (votesYes)
        {
            JLOG (j_.debug())
                    << "Peer " << peer << " votes YES on " << tx_.id();
            ++yays_;
        }
        else
        {
            JLOG (j_.debug())
                    << "Peer " << peer << " votes NO on " << tx_.id();
            ++nays_;
        }
    }
    // changes vote to yes
    else if (votesYes && !res.first->second)
    {
        JLOG (j_.debug())
                << "Peer " << peer << " now votes YES on " << tx_.id();
        --nays_;
        ++yays_;
        res.first->second = true;
    }
    // changes vote to no
    else if (!votesYes && res.first->second)
    {
        JLOG (j_.debug())
                << "Peer " << peer << " now votes NO on " << tx_.id();
        ++nays_;
        --yays_;
        res.first->second = false;
    }
}

// Remove a peer's vote on this disputed transasction
template <class Tx_t, class NodeID_t>
void DisputedTx<Tx_t, NodeID_t>::unVote (NodeID_t const& peer)
{
    auto it = votes_.find (peer);

    if (it != votes_.end ())
    {
        if (it->second)
            --yays_;
        else
            --nays_;

        votes_.erase (it);
    }
}

template <class Tx_t, class NodeID_t>
bool DisputedTx<Tx_t, NodeID_t>::updateVote (int percentTime, bool proposing)
{
    if (ourVote_ && (nays_ == 0))
        return false;

    if (!ourVote_ && (yays_ == 0))
        return false;

    bool newPosition;
    int weight;

    if (proposing) // give ourselves full weight
    {
        // This is basically the percentage of nodes voting 'yes' (including us)
        weight = (yays_ * 100 + (ourVote_ ? 100 : 0)) / (nays_ + yays_ + 1);

        // VFALCO TODO Rename these macros and turn them into language
        //             constructs.  consolidate them into a class that collects
        //             all these related values.
        //
        // To prevent avalanche stalls, we increase the needed weight slightly
        // over time.
        if (percentTime < AV_MID_CONSENSUS_TIME)
            newPosition = weight >  AV_INIT_CONSENSUS_PCT;
        else if (percentTime < AV_LATE_CONSENSUS_TIME)
            newPosition = weight > AV_MID_CONSENSUS_PCT;
        else if (percentTime < AV_STUCK_CONSENSUS_TIME)
            newPosition = weight > AV_LATE_CONSENSUS_PCT;
        else
            newPosition = weight > AV_STUCK_CONSENSUS_PCT;
    }
    else
    {
        // don't let us outweigh a proposing node, just recognize consensus
        weight = -1;
        newPosition = yays_ > nays_;
    }

    if (newPosition == ourVote_)
    {
        JLOG (j_.info())
                << "No change (" << (ourVote_ ? "YES" : "NO") << ") : weight "
                << weight << ", percent " << percentTime;
        JLOG (j_.debug()) << getJson ();
        return false;
    }

    ourVote_ = newPosition;
    JLOG (j_.debug())
            << "We now vote " << (ourVote_ ? "YES" : "NO")
            << " on " << tx_.id();
    JLOG (j_.debug()) << getJson ();
    return true;
}

template <class Tx_t, class NodeID_t>
Json::Value DisputedTx<Tx_t, NodeID_t>::getJson () const
{
    using std::to_string;

    Json::Value ret (Json::objectValue);

    ret["yays"] = yays_;
    ret["nays"] = nays_;
    ret["our_vote"] = ourVote_;

    if (!votes_.empty ())
    {
        Json::Value votesj (Json::objectValue);
        for (auto& vote : votes_)
            votesj[to_string (vote.first)] = vote.second;
        ret["votes"] = std::move (votesj);
    }

    return ret;
}

} // ripple

#endif
