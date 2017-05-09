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

#include <ripple/nodestore/impl/DatabaseShardImp.h>
#include <ripple/basics/random.h>
#include <ripple/beast/core/LexicalCast.h>

namespace ripple {
namespace NodeStore {

DatabaseShardImp::DatabaseShardImp(NodeStore::Database& nodeStore,
    Section const& config, Scheduler& scheduler, boost::filesystem::path dir,
        std::uint64_t maxDiskSpace, beast::Journal journal)
    : nodeStore_(nodeStore)
    , config_(config)
    , scheduler_(scheduler)
    , dir_(std::move(dir))
    , maxDiskSpace_(maxDiskSpace)
    , j_(journal)
{
}

DatabaseShardImp::~DatabaseShardImp()
{
    std::lock_guard<std::mutex> l(m_);
    saveMaster();
}

bool
DatabaseShardImp::init()
{
    using namespace boost::filesystem;
    auto const master = dir_ / "master.bin";
    if (! exists(master))
    {
        if (maxDiskSpace_ > space(dir_).free)
        {
            JLOG(j_.warn()) << "Insufficient disk space";
        }
        // Find db file descriptor requirements
        auto s = std::make_unique<Shard>(1);
        if (! s->open(config_, scheduler_, dir_, j_))
            return false;
        s->setDeletePath();
        fdLimit_ = 1 + (s->fdlimit() *
            std::max<std::uint64_t>(1, maxDiskSpace_ / avgShardSize_));
        return true;
    }

    std::ifstream ifs(master.string(), std::ios::binary);
    if (! ifs.is_open())
    {
        JLOG(j_.error()) << "Unable to open master file";
        return false;
    }
    usedDiskSpace_ = file_size(master);

    boost::archive::binary_iarchive ia(ifs);
    ia & shards_;
    // Open shards
    auto open =[&](std::map<std::uint32_t, boost::shared_ptr<Shard>> shards){
        for (auto const& s : shards)
        {
            if (! s.second->open(config_, scheduler_, dir_, j_))
                return false;
            usedDiskSpace_ += s.second->fileSize();
        }
        return true;
    };
    if (! open(shards_.complete) || ! open(shards_.incomplete))
        return false;

    // Remove shards not in master
    for (auto const& de : directory_iterator(dir_))
    {
        if (is_directory(de))
        {
            auto const i = beast::lexicalCastThrow<std::uint32_t>(
                de.path().stem().string());
            if (shards_.complete.find(i) == shards_.complete.end() &&
                shards_.incomplete.find(i) == shards_.incomplete.end())
            {
                remove_all(de);
            }
        }
    }

    std::uint32_t filesPerShard {0};
    if (! shards_.complete.empty())
    {
        avgShardSize_ = 0;
        for (auto const& s : shards_.complete)
            avgShardSize_ += s.second->fileSize();
        avgShardSize_ /= shards_.complete.size();
        filesPerShard = shards_.complete.begin()->second->fdlimit();
        setComplete();
    }
    else if(! shards_.incomplete.empty())
        filesPerShard = shards_.incomplete.begin()->second->fdlimit();
    fdLimit_ = 1 + (filesPerShard *
        (shards_.complete.size() + shards_.incomplete.size()));

    if (usedDiskSpace_ >= maxDiskSpace_)
    {
        JLOG(j_.trace()) << "Maximum size reached";
        canAdd_ = false;
    }
    else
    {
        auto const sz = maxDiskSpace_ - usedDiskSpace_;
        if (sz > space(dir_).free)
        {
            JLOG(j_.warn()) << "Insufficient disk space";
        }
        fdLimit_ += (filesPerShard * (sz / avgShardSize_));
    }
    return true;
}

boost::optional<std::uint32_t>
DatabaseShardImp::prepare(std::uint32_t maxSeq)
{
    std::lock_guard<std::mutex> l(m_);
    auto const sz = shards_.incomplete.size();
    if (! canAdd_ || sz > 0)
    {
        if (sz == 1)
            return shards_.incomplete.begin()->second->prepare(maxSeq);
        if (sz > 1)
        {
            std::vector<boost::shared_ptr<Shard>> v;
            v.reserve(shards_.incomplete.size());
            for (auto const& s : shards_.incomplete)
                v.push_back(s.second);
            std::shuffle(v.begin(), v.end(), default_prng());
            for (auto const& s : v)
                if (auto r = s->prepare(maxSeq))
                    return r;
        }
        return boost::none;
    }

    // Create a new shard to acquire
    if (usedDiskSpace_ + avgShardSize_ > maxDiskSpace_)
    {
        JLOG(j_.trace()) << "Maximum size reached";
        canAdd_ = false;
        return boost::none;
    }
    if (avgShardSize_ > boost::filesystem::space(dir_).free)
    {
        JLOG(j_.warn()) << "Insufficient disk space";
        canAdd_ = false;
        return boost::none;
    }

    auto const shardIndexToAdd = findShardIndexToAdd(seqToShardIndex(maxSeq));
    if (! shardIndexToAdd)
    {
        JLOG(j_.trace()) << "no new shards to add";
        canAdd_ = false;
        return boost::none;
    }

    auto s = shards_.incomplete.emplace(*shardIndexToAdd,
        boost::make_shared<Shard>(*shardIndexToAdd));
    if (! s.first->second->open(config_, scheduler_, dir_, j_))
    {
        s.first->second->setDeletePath();
        shards_.incomplete.erase(*shardIndexToAdd);
        return boost::none;
    }
    saveMaster();
    return s.first->second->prepare(maxSeq);
}

bool
DatabaseShardImp::store(std::shared_ptr<Ledger const> const& ledger)
{
    auto const seq = ledger->info().seq;
    auto const shardIndex = seqToShardIndex(seq);
    std::lock_guard<std::mutex> l(m_);
    auto it = shards_.incomplete.find(shardIndex);
    if (it == shards_.incomplete.end())
    {
        JLOG(j_.trace()) << "shard not being acquired";
        return false;
    }
    if(it->second->hasLedger(seq))
        return true;

    auto next = it->second->lastStored();
    if (next && next->info().parentHash != ledger->info().hash)
        return false;
    if (ledger->info().accountHash.isZero())
    {
        JLOG(j_.error()) << "ledger has a zero account hash";
        return false;
    }

    Batch batch;
    bool error = false;
    auto f = [&](SHAMapAbstractNode& node) {
            if (auto no = nodeStore_.fetch(node.getNodeHash().as_uint256()))
                batch.emplace_back(std::move(no));
            else
                error = true;
            return ! error;
        };
    // Copy state map
    if (ledger->stateMap().getHash().isNonZero())
    {
        if (! ledger->stateMap().isValid())
        {
            JLOG(j_.warn()) << "invalid state map";
            return false;
        }
        if (next)
        {
            if (! next->stateMap().isValid())
            {
                JLOG(j_.warn()) << "invalid state map";
                return false;
            }
            auto have = next->stateMap().snapShot(false);
            ledger->stateMap().snapShot(false)->visitDifferences(&(*have), f);
        }
        else
            ledger->stateMap().snapShot(false)->visitNodes(f);
        if (error)
            return false;
    }
    // Copy transaction map
    if (ledger->info().txHash.isNonZero())
    {
        if (! ledger->txMap().isValid())
        {
            JLOG(j_.warn()) << "invalid transactions map";
            return false;
        }
        if (next)
        {
            if (! next->txMap().isValid())
            {
                JLOG(j_.warn()) << "invalid transactions map";
                return false;
            }
            auto have = next->txMap().snapShot(false);
            ledger->txMap().snapShot(false)->visitDifferences(&(*have), f);
        }
        else
            ledger->txMap().snapShot(false)->visitNodes(f);
        if (error)
            return false;
    }

    auto const sz = it->second->fileSize();
    auto destroy = [&](boost::shared_ptr<Shard>& s) {
            usedDiskSpace_ -= s->fileSize();
            s->setDeletePath();
            s->close();
            shards_.incomplete.erase(s->index());
            saveMaster();
        };
    if (! batch.empty() && ! it->second->store(batch, j_))
    {
        // Delete corrupt shard
        destroy(it->second);
        return false;
    }
    // Save ledger header last
    if (! it->second->storeLedgerHeader(ledger, j_))
    {
        // Delete corrupt shard
        destroy(it->second);
        return false;
    }

    usedDiskSpace_ -= sz;
    usedDiskSpace_ += it->second->fileSize();

    if (it->second->complete())
    {
        shards_.complete.emplace(shardIndex, std::move(it->second));
        shards_.incomplete.erase(shardIndex);
        saveMaster();
        avgShardSize_ = 0;
        for (auto const& s : shards_.complete)
            avgShardSize_ += s.second->fileSize();
        avgShardSize_ /= shards_.complete.size();
        setComplete();
    }
    return true;
}

std::shared_ptr<NodeObject>
DatabaseShardImp::fetch(uint256 const& hash, std::uint32_t seq)
{
    auto const shardIndex = seqToShardIndex(seq);
    std::lock_guard<std::mutex> l(m_);
    auto it = shards_.complete.find(shardIndex);
    if (it == shards_.complete.end())
    {
        it = shards_.incomplete.find(shardIndex);
        if (it == shards_.incomplete.end())
            return nullptr;
    }
    return it->second->fetch(hash, j_);
}

bool
DatabaseShardImp::hasLedger(std::uint32_t seq)
{
    auto const shardIndex = seqToShardIndex(seq);
    std::lock_guard<std::mutex> l(m_);
    auto it = shards_.complete.find(shardIndex);
    if (it == shards_.complete.end())
    {
        it = shards_.incomplete.find(shardIndex);
        if (it == shards_.incomplete.end())
            return false;
    }
    return it->second->hasLedger(seq);
}

std::string
DatabaseShardImp::getCompleteShards()
{
    std::lock_guard<std::mutex> l(m_);
    return complete_;
}

// Assumes lock is held
boost::optional<std::uint32_t>
DatabaseShardImp::findShardIndexToAdd(std::uint32_t maxShardIndex)
{
    assert(maxShardIndex >= detail::genesisShardIndex);
    auto const numShards = shards_.complete.size() + shards_.incomplete.size();
    assert(numShards <= maxShardIndex + 1);

    // If equal, have all the shards
    if (numShards >= maxShardIndex + 1)
        return boost::none;

    if (maxShardIndex < 1024 || float(numShards) / maxShardIndex > 0.5f)
    {
        // Small or mostly full index space to sample
        // Find the available indexes and select one at random
        std::vector<std::uint32_t> available;
        available.reserve(maxShardIndex - numShards + 1);
        for (std::uint32_t i = detail::genesisShardIndex;
            i <= maxShardIndex; ++i)
        {
            if (! shards_.complete.count(i) && ! shards_.incomplete.count(i))
                available.push_back(i);
        }
        if (! available.empty())
            return available[rand_int({0}, available.size() - 1)];
    }

    // Large, sparse index space to sample
    // Keep choosing indexes at random until an available one is found
    // chances of running more than 30 times is less than 1 in a billion
    for (int i = 0; i < 40; ++i)
    {
        auto const r = rand_int(detail::genesisShardIndex, maxShardIndex);
        if (! shards_.complete.count(r) && ! shards_.incomplete.count(r))
            return r;
    }
    assert(0);
    return boost::none;
}

// Assumes lock is held
void
DatabaseShardImp::setComplete()
{
    complete_.clear();
    for (auto it = shards_.complete.begin(); it != shards_.complete.end(); ++it)
    {
        if (it == shards_.complete.begin())
            complete_ = beast::lexicalCastThrow<std::string>(it->first);
        else
        {
            if (it->first - std::prev(it)->first > 1)
            {
                if (complete_.back() == '-')
                    complete_ += beast::lexicalCastThrow<std::string>(
                        std::prev(it)->first);
                complete_ += "," +
                    beast::lexicalCastThrow<std::string>(it->first);
            }
            else
            {
                if (complete_.back() != '-')
                    complete_ += "-";
                if (std::next(it) == shards_.complete.end())
                    complete_ += beast::lexicalCastThrow<std::string>(it->first);
            }
        }
    }
}

// Assumes lock is held
void
DatabaseShardImp::saveMaster()
{
    using namespace boost::filesystem;
    static auto const master = dir_ / "master.bin";
    if (! shards_.incomplete.empty() || ! shards_.complete.empty())
    {
        std::ofstream ofs(master.string(), std::ios::binary | std::ios::trunc);
        if (ofs.is_open())
        {
            boost::archive::binary_oarchive oa(ofs);
            oa & shards_;
        }
        else
            JLOG(j_.error()) << "Unable to save master";
    }
    else
        remove(master);
}

} // NodeStore
} // ripple
