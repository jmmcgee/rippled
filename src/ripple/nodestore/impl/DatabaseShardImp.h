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

#ifndef RIPPLE_NODESTORE_DATABASESHARDIMP_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASESHARDIMP_H_INCLUDED

#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/nodestore/impl/Shard.h>

#include <boost/serialization/shared_ptr.hpp>

namespace ripple {
namespace NodeStore {

class DatabaseShardImp
    : public DatabaseShard
{
private:
    using error_code = boost::system::error_code;

public:
    DatabaseShardImp(NodeStore::Database&, Section const& config,
        Scheduler& scheduler, boost::filesystem::path dir,
            std::uint64_t maxDiskSpace, beast::Journal journal);

    ~DatabaseShardImp();

    bool
    init() override;

    boost::optional<std::uint32_t>
    prepare(std::uint32_t maxSeq) override;

    bool
    store(std::shared_ptr<Ledger const> const& ledger) override;

    std::shared_ptr<NodeObject>
    fetch(uint256 const& hash, std::uint32_t seq) override;

    bool
    hasLedger(std::uint32_t seq) override;

    int
    fdlimit() const override
    {
        return fdLimit_;
    }

private:
    struct Shards
    {
        std::map<std::uint32_t, boost::shared_ptr<Shard>> complete;
        // TODO MPORTILLA Its best to acquire one shard at a time.
        // We may want to replace `incomplete` with a single shared_ptr
        std::map<std::uint32_t, boost::shared_ptr<Shard>> incomplete;

    private:
        friend class boost::serialization::access;
        template<class Archive>
        void
        serialize(Archive& ar, const unsigned int version)
        {
            ar & complete;
            ar & incomplete;
        }
    };

    std::mutex m_;
    Shards shards_;
    NodeStore::Database& nodeStore_;
    Section const& config_;
    Scheduler& scheduler_;
    boost::filesystem::path dir_;
    int fdLimit_ {0};

    // Maximum disk space in bytes the DB can use.
    // New shards will be added until all shards are stored
    // or maxDiskSpace is exceeded.
    std::uint64_t const maxDiskSpace_;

    // Disk space used to store the shards (in bytes)
    std::uint64_t usedDiskSpace_ {0};

    // Average disk space a shard requires (in bytes)
    std::uint64_t avgShardSize_ {ledgersPerShard * (512ull * 1024)};

    bool canAdd_ {true};
    beast::Journal j_;

    // Finds a random shard index not in the collection
    boost::optional<std::uint32_t>
    getShardIndexToAdd(std::uint32_t maxShardIndex);

    void
    saveMaster();
};

} // NodeStore
} // ripple

#endif
