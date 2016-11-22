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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXLEDGER_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXLEDGER_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <memory>

namespace ripple {

// A ledger (validated or not) used as part of the consensus process
class RCLCxLedger
{
public:
    using id_type = LedgerHash;

    // Do we need this or can we force LedgerConsens to start with some ledger?
    RCLCxLedger() = default;

    RCLCxLedger(std::shared_ptr<Ledger const> const & l) : ledger_{ l } {}

    auto seq() const
    {
        return ledger_->info().seq;
    }

    auto closeTimeResolution() const
    {
        return ledger_->info().closeTimeResolution;
    }

    bool getCloseAgree() const
    {
        return ripple::getCloseAgree(ledger_->info());
    }

    auto ID() const
    {
        return ledger_->info().hash;
    }

    auto parentID() const
    {
        return ledger_->info().parentHash;
    }

    auto closeTime() const
    {
        return ledger_->info().closeTime;
    }

    auto parentCloseTime() const
    {
        return ledger_->info().parentCloseTime;
    }

    Json::Value getJson() const
    {
        return ripple::getJson(*ledger_);
    }

    auto const & peek() const
    {
        return ledger_;
    }

protected:

    // TODO: Make this shared_ptr<ReadView const> .. requires ability to create
    // a new ledger from a readview?
    std::shared_ptr<Ledger const> ledger_;

};

}
#endif
