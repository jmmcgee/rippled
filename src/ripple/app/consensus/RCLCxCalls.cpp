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
#include <ripple/app/ledger/impl/ConsensusImp.h>

namespace ripple {

RCLCxCalls::RCLCxCalls (Application& app, ConsensusImp& consensus, beast::Journal& j)
        : app_ (app)
        , consensus_ (consensus)
        , j_ (j)
        , valPublic_ (app_.config().VALIDATION_PUB)
        , valSecret_ (app_.config().VALIDATION_PRIV)
{ }


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

void RCLCxCalls::propose (RCLCxPos const& position)
{
    JLOG (j_.trace()) << "We propose: " <<
        (position.isBowOut () ?  std::string ("bowOut") :
            to_string (position.getCurrentHash ()));

    protocol::TMProposeSet prop;

    prop.set_currenttxhash (position.getCurrentHash().begin(),
        256 / 8);
    prop.set_previousledger (position.getPrevLedger().begin(),
        256 / 8);
    prop.set_proposeseq (position.getProposeSeq ());
    prop.set_closetime (
        position.getCloseTime().time_since_epoch().count());

    prop.set_nodepubkey (valPublic_.data(), valPublic_.size());

    auto signingHash = sha512Half(
        HashPrefix::proposal,
        std::uint32_t(position.getSequence()),
        position.getCloseTime().time_since_epoch().count(),
        position.getPrevLedger(), position.getCurrentHash());

    auto sig = signDigest (
        valPublic_, valSecret_, signingHash);

    prop.set_signature (sig.data(), sig.size());

    app_.overlay().send(prop);
}

void RCLCxCalls::getProposals (LedgerHash const& prevLedger,
    std::function <bool (RCLCxPos const&)> func)
{
    auto proposals = consensus_.getStoredProposals (prevLedger);
    for (auto& prop : proposals)
    {
        if (func (prop))
        {
            auto& proposal = prop.peek();

            protocol::TMProposeSet prop;

            prop.set_proposeseq (
                proposal.getProposeSeq ());
            prop.set_closetime (
                proposal.getCloseTime ().time_since_epoch().count());

            prop.set_currenttxhash (
                proposal.getCurrentHash().begin(), 256 / 8);
            prop.set_previousledger (
                proposal.getPrevLedger().begin(), 256 / 8);

            auto const pk = proposal.getPublicKey().slice();
            prop.set_nodepubkey (pk.data(), pk.size());

            auto const sig = proposal.getSignature();
            prop.set_signature (sig.data(), sig.size());

            app_.overlay().relay (prop, proposal.getSuppressionID ());
        }
    }
}


// First bool is whether or not we can propose
// Second bool is whether or not we can validate
std::pair <bool, bool> RCLCxCalls::getMode (bool correctLCL)
{
    bool propose = false;
    bool validate = false;


    if (! app_.getOPs().isNeedNetworkLedger() && (valPublic_.size() != 0))
    {
        // We have a key, and we have some idea what the ledger is
        validate = true;

        // propose only if we're in sync with the network
        propose = app_.getOPs().getOperatingMode() == NetworkOPs::omFULL;
    }
    if (! correctLCL)
        consensus_.setProposing (false, false);
    else
        consensus_.setProposing (propose, validate);

    return { propose, validate };
}


} // namespace ripple
