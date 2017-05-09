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

#ifndef RIPPLE_NODESTORE_DATABASESHARD_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASESHARD_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>

#include <boost/optional.hpp>

#include <memory>

namespace ripple {
namespace NodeStore {

/** Return the coresponding shard index of the ledger sequence */
constexpr
std::uint32_t
seqToShardIndex(std::uint32_t const seq)
{
    return (seq - 1) / ledgersPerShard;
}

/**
   A collection of historical shards
*/
class DatabaseShard
{
public:
    virtual ~DatabaseShard() = default;

    /**
        initialize the database

        @return `true` if the database initialized without error
    */
    virtual bool init() = 0;

    /**
       prepare to store a new ledger in the shard

       @param maxSeq the index of the maximum valid ledgers
       @return if a ledger should be fetched and stored, then returns the ledger
               index of the ledger to request. Otherwise returns boost::none.
               Some reasons this may return boost::none are: this database does
               not store shards, all shards are are stored and full, max allowed
               disk space would be exceeded, or a ledger was recently requested
               and not enough time has passed between requests.
       @implNote adds a new writable shard if necessary
    */
    virtual boost::optional<std::uint32_t> prepare(std::uint32_t maxSeq) = 0;

    /**
       store a ledger

       @param ledger ledger to store
       @return `true` if the ledger was stored
    */
    virtual bool store(std::shared_ptr<Ledger const> const& ledger) = 0;

    /**
       fetch a node object

       @param hash key to the object to fetch
       @param seq index to ledger to fetch. This is needed to find the
              correct shard index.
       @return on success, returns the NodeObject with the given hash,
               otherwise returns a null shared_ptr
    */
    virtual std::shared_ptr<NodeObject> fetch(
        uint256 const& hash, std::uint32_t seq) = 0;

    /**
       query if a ledger with the given index is stored

       @param seq index to check if stored
       @return `true` if the ledger is stored
    */
    virtual bool hasLedger(std::uint32_t seq) = 0;

    /**
       query which complete shards are stored

       @return the indexes of complete shards
    */
    virtual std::string getCompleteShards() = 0;


    /**
       query how required file descriptors

       @return the number of files needed
    */
    virtual int fdlimit() const = 0;
};

}
}

#endif
