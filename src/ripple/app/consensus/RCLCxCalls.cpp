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

#include <ripple/app/consensus/RCLCxCalls.h>

namespace ripple {

uint256 RCLCxCalls::getLCL (
    uint256 const& currentLedger,
    uint256 const& priorLedger,
    bool believedCorrect)
{
    // Get validators that are on our ledger, or  "close" to being on
    // our ledger.
    auto vals =
        app_.getValidations().getCurrentValidations(
            currentLedger, priorLedger,
            app_.getLedgerMaster().getValidLedgerIndex());

    uint256 netLgr = currentLedger;
    int netLgrCount = 0;
    for (auto& it : vals)
    {
        if ((it.second.first > netLgrCount) ||
            ((it.second.first == netLgrCount) && (it.first == priorLedger)))
        {
           netLgr = it.first;
           netLgrCount = it.second.first;
        }
    }

    if (believedCorrect && (netLgr != currentLedger))
    {
#if 0 // FIXME
        if (auto stream = j_.debug())
        {
            for (auto& it : vals)
                stream
                    << "V: " << it.first << ", " << it.second.first;
        }
#endif

        app_.getOPs().consensusViewChange();
    }

    return netLgr;
}

void RCLCxCalls::shareSet (RCLTxSet const& set)
{
    app_.getInboundTransactions().giveSet (set.getID(),
        set.map(), false);
}


} // namespace ripple
