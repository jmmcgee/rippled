//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

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
#include <test/csf/ledgers.h>

#include <sstream>

namespace ripple {
namespace test {
namespace csf {

Ledger::Instance const Ledger::genesis{};

Json::Value
Ledger::getJson() const
{
    Json::Value res(Json::objectValue);
    res["id"] = static_cast<ID::value_type>(id());
    res["seq"] = static_cast<Seq::value_type>(seq());
    return res;
}

LedgerOracle::LedgerOracle()
{
    instances_.insert(InstanceEntry{Ledger::genesis, nextID()});
}

Ledger::ID
LedgerOracle::nextID() const
{
    return Ledger::ID{static_cast<Ledger::ID::value_type>(instances_.size())};
}

Ledger
LedgerOracle::accept(Ledger const & curr, TxSetType const& txs,
    NetClock::duration closeTimeResolution,
    NetClock::time_point const& consensusCloseTime,
    bool closeTimeAgree)
{
    Ledger::Instance next(*curr.instance_);
    next.txs.insert(txs.begin(), txs.end());
    next.seq = curr.seq() + 1;
    next.closeTimeResolution = closeTimeResolution;
    next.closeTime = effCloseTime(
        consensusCloseTime, closeTimeResolution, curr.parentCloseTime());
    next.closeTimeAgree = closeTimeAgree;
    next.parentCloseTime = curr.closeTime();
    next.parentID = curr.id();
    auto it = instances_.left.find(next);
    if (it == instances_.left.end())
    {
        using Entry = InstanceMap::left_value_type;
        it = instances_.left.insert(Entry{next, nextID()}).first;
    }
    return Ledger(it->second, &(it->first));
}


/** Switch to a new current ledger

    Switch to a new current ledger, recording a jump if the new ledger
    is not the child of the current ledger.

    @param now When the switcho ccurs
    @param f The new ledger

*/
void
LedgerState::switchTo(NetClock::time_point const now, Ledger const& f)
{
    // No switch to the same ledger
    if (current_.id() == f.id())
        return;

    // This is a jump if current_ is not the parent of f
    if (f.parentID() != current_.id())
    {
        jumps_.emplace_back(Jump{now, current_.id(), f.id()});
    }

    current_ = f;
}

}  // namespace csf
}  // namespace test
}  // namespace ripple
