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

#ifndef RIPPLE_NODESTORE_SHARD_H_INCLUDED
#define RIPPLE_NODESTORE_SHARD_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/nodestore/Scheduler.h>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/filesystem.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/binary_object.hpp>

#include <chrono>

namespace ripple {
namespace NodeStore {
namespace detail {

// Return the first ledger sequence of the shard index
constexpr
std::uint32_t
firstSeq(std::uint32_t const shardIndex)
{
    return 1 + (shardIndex * ledgersPerShard);
}

// Return the last ledger sequence of the shard index
constexpr
std::uint32_t
lastSeq(std::uint32_t const shardIndex)
{
    return (shardIndex + 1) * ledgersPerShard;
}

static constexpr auto genesisShardIndex = seqToShardIndex(32570u);
static constexpr auto genesisNumLedgers = ledgersPerShard -
    (32570u - firstSeq(genesisShardIndex));

} // detail

/* A range of historical ledgers backed by a nodestore.
   Shards are indexed and store `ledgersPerShard`.
   Shard `i` stores ledgers starting with index: `1 + (i * ledgersPerShard)`
   and ending with index: `(i + 1) * ledgersPerShard`.
   Once a shard has all its ledgers, it is marked as read only
   and is never written to again.
*/
class Shard
{
private:
    using error_code = boost::system::error_code;
    using clock = std::chrono::steady_clock;

public:
    explicit
    Shard(std::uint32_t index) : index_ {index}{}

    ~Shard()
    {
        close();
    }

    bool
    open(Section config, Scheduler& scheduler,
        boost::filesystem::path dir, beast::Journal& j);

    void
    close();

    std::shared_ptr<NodeObject>
    fetch(uint256 const& hash, beast::Journal& j);

    bool
    store(Batch& batch, beast::Journal& j);

    bool
    storeLedgerHeader(std::shared_ptr<Ledger const> const& ledger,
        beast::Journal& j);

    boost::optional<std::uint32_t>
    prepare(std::uint32_t maxLedger);

    bool
    hasLedger(std::uint32_t seq) const;

    std::uint32_t
    index() const
    {
        return index_;
    }

    bool
    complete() const
    {
        return complete_;
    }

    std::uint64_t
    fileSize() const
    {
        return fileSize_;
    }

    std::uint32_t
    fdlimit() const;

    std::shared_ptr<Ledger const>
    lastStored()
    {
        return lastStored_;
    }

    // Remove contents on disk upon destruction
    void
    setDeletePath();

private:

    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & index_;
        ar & complete_;
        //ar & boost::serialization::make_binary_object(&last_, sizeof(last_));
        ar & storedIndexes_;
    }

    Shard(){}

    // paths to the database files
    boost::filesystem::path dir_;

    std::uint32_t index_ {0};
    std::uint32_t firstSeq_ {0};
    std::uint32_t lastSeq_ {0};

    bool complete_ {false};
    std::shared_ptr<Ledger const> lastStored_;
    clock::time_point last_;

    // Ledgers currently stored. Empty when shard is complete.
    RangeSet<std::uint32_t> storedIndexes_;

    std::uint32_t maxStored_ {0};
    std::uint32_t numStored_ {0};
    clock::time_point request_;
    std::uint64_t fileSize_ {0};
    std::unique_ptr<Backend> db_;

    std::uint64_t
    calcFileSize() const;
};

} // NodeStore
} // ripple

#endif
