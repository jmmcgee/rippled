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

#include <BeastConfig.h>

#include <ripple/nodestore/impl/Shard.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/protocol/HashPrefix.h>

namespace ripple {
namespace NodeStore {

bool
Shard::open(Section config, Scheduler& scheduler,
    boost::filesystem::path dir, beast::Journal& j)
{
    // Shard index needs to be effective genesis ledger or higher
    assert(! db_ && index_ >= detail::genesisShardIndex);
    using namespace boost::filesystem;
    dir_ = dir / std::to_string(index_);
    config.set("path", dir_.string());
    try
    {
        db_ = Manager::instance().make_Backend(config, scheduler, j);
    }
    catch (std::exception const& e)
    {
        JLOG(j.error()) << "Shard: Exception, " << e.what();
        return false;
    }

    firstSeq_ = std::max(32570u, detail::firstSeq(index_));
    lastSeq_ = detail::lastSeq(index_);
    fileSize_ = calcFileSize();
    if (! complete_)
    {
        maxStored_ = index_ == detail::genesisShardIndex
            ? detail::genesisNumLedgers : ledgersPerShard;
        numStored_ = boost::icl::length(storedIndexes_);
    }
    return true;
}

void
Shard::close()
{
    if (db_)
        db_->close();
}

std::shared_ptr<NodeObject>
Shard::fetch(uint256 const& hash, beast::Journal& j)
{
    assert(db_);
    std::shared_ptr<NodeObject> no;
    Status const status = db_->fetch(hash.begin(), &no);
    switch (status)
    {
    case ok:
    case notFound:
        break;

    case dataCorrupt:
        JLOG(j.fatal()) << "Corrupt NodeObject #" << hash;
        break;

    default:
        JLOG(j.warn()) << "Unknown status=" << status;
        break;
    }
    return no;
}

bool
Shard::store(Batch& batch, beast::Journal& j)
{
    assert(db_ && ! complete_);
    try
    {
        db_->storeBatch(batch);
    }
    catch (std::exception const& e)
    {
        JLOG(j.error()) << "Shard: Exception, " << e.what();
        return false;
    }
    return true;
}

bool
Shard::storeLedgerHeader(std::shared_ptr<Ledger const> const& ledger,
    beast::Journal& j)
{
    assert(db_ && ! complete_);
    Serializer s(128);
    s.add32(HashPrefix::ledgerMaster);
    addRaw(ledger->info(), s);
    auto no = NodeObject::createObject(hotLEDGER,
        std::move(s.modData()), ledger->info().hash);
    try
    {
        db_->store(no);
    }
    catch (std::exception const& e)
    {
        JLOG(j.error()) << "Shard: Exception, " << e.what();
        return false;
    }

    lastStored_ = ledger;
    last_ = clock::now();
    storedIndexes_.insert(ledger->info().seq);
    request_ = {};
    fileSize_ = calcFileSize();
    if (++numStored_ == maxStored_)
    {
        complete_ = true;
        storedIndexes_.clear();
    }
    return true;
}

boost::optional<std::uint32_t>
Shard::prepare(std::uint32_t ledgerIndex)
{
    assert(! complete_);
    // Prevent too many requests in a short period
    using namespace std::chrono_literals;
    auto const now = clock::now();
    if (now - request_ < 10s)
        return boost::none;
    request_ = now;

    auto const result = prevMissing(storedIndexes_,
        1 + std::min(ledgerIndex, lastSeq_));
    if (result == boost::none || *result < firstSeq_)
        return boost::none;
    return result;
}

bool
Shard::hasLedger(std::uint32_t seq) const
{
    if (seq < firstSeq_ || seq > lastSeq_)
        return false;
    if (complete_)
        return true;
    return boost::icl::contains(storedIndexes_,seq);
}

std::uint32_t
Shard::fdlimit() const
{
    if (db_)
        return db_->fdlimit();
    return 0;
}

void
Shard::setDeletePath()
{
    if (db_)
        db_->setDeletePath();
}

std::uint64_t
Shard::calcFileSize() const
{
    using namespace boost::filesystem;
    std::uint64_t sz = 0;
    for (auto const& de : directory_iterator(dir_))
        if (! is_directory(de))
            sz += file_size(de);
    return sz;
}

} // NodeStore
} // ripple
