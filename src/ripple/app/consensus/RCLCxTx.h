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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXTX_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXTX_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/app/misc/CanonicalTXSet.h>

namespace ripple {

// Transactions, as seen by the consensus code in the rippled app
class RCLCxTx
{
public:
    using id_type = uint256;

    RCLCxTx(SHAMapItem const& txn) : txn{ txn }
    { }

    id_type const&
    ID() const
    {
        return txn.key ();
    }

    SHAMapItem const txn;
};

// Sets of transactions
// as seen by the consensus code in the rippled app
class RCLTxSet
{
public:
    using id_type = uint256;
    using tx_type = RCLCxTx;

    RCLTxSet (std::shared_ptr<SHAMap> m) :
        map{ std::move(m) }
    {
        assert(map);
    }

    bool
    insert (tx_type const& t)
    {
        return map->addItem (
            SHAMapItem {t.ID(), t.txn.peekData()},
            true, false);
    }

    bool
    erase (tx_type::id_type const& entry)
    {
        return map->delItem (entry);
    }

    bool
    exists(tx_type::id_type const& entry) const
    {
        return map->hasItem (entry);
    }

    auto
    find(tx_type::id_type const& entry) const
    {
        return map->peekItem (entry);
    }

    id_type
    ID() const
    {
        return map->getHash().as_uint256();
    }

    std::map <tx_type::id_type, bool>
    diff (RCLTxSet const& j) const
    {
        SHAMap::Delta delta;

        // Bound the work we do in case of a malicious
        // map from a trusted validator
        map->compare (*(j.map), delta, 65536);

        std::map <uint256, bool> ret;
        for (auto const& item : delta)
        {
            assert ( (item.second.first && ! item.second.second) ||
                     (item.second.second && ! item.second.first) );

            ret[item.first] = static_cast<bool> (item.second.first);
        }
        return ret;
    }

    std::shared_ptr <SHAMap> map;
};

}
#endif
