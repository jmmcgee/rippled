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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXCALLS_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXCALLS_H_INCLUDED

#include <ripple/protocol/UintTypes.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/NetworkOPs.h>

namespace ripple {

class RCLCxCalls
{
public:

    RCLCxCalls (Application& app, beast::Journal& j)
        : app_ (app)
        , j_ (j)
        , valPublic_ (app_.config().VALIDATION_PUB)
        , valSecret_ (app_.config().VALIDATION_PRIV)
    { }

    uint256 getLCL (
        uint256 const& currentLedger,
        uint256 const& priorLedger,
        bool believedCorrect);

    void shareSet (RCLTxSet const& set);

    void propose (RCLCxPos const& position);

protected:

    Application& app_;
    beast::Journal j_;
    PublicKey valPublic_;
    SecretKey valSecret_;
};

} // namespace ripple
#endif
