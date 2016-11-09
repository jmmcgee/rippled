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
#include <BeastConfig.h>
#include <ripple/app/consensus/RCLCxCalls.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/consensus/RCLCxLedger.h>
#include <ripple/app/consensus/RCLCxPos.h>
#include <ripple/app/consensus/RCLCxTx.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/digest.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/basics/make_lock.h>
#include <ripple/app/ledger/impl/ConsensusImp.h>
#include <ripple/app/ledger/LocalTxs.h>

namespace ripple {

RCLCxCalls::RCLCxCalls (
    Application& app,
    ConsensusImp & consensus,
    FeeVote& feeVote,
    LedgerMaster& ledgerMaster,
    LocalTxs & localTxs,
    beast::Journal& j)
        : app_ (app)
        , consensus_ (consensus)
        , feeVote_ (feeVote)
        , ledgerMaster_ (ledgerMaster)
        , localTxs_(localTxs)
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

std::pair <RCLTxSet, RCLCxPos>
RCLCxCalls::makeInitialPosition (RCLCxLedger const & prevLedgerT,
        bool proposing,
        bool correctLCL,
        NetClock::time_point closeTime,
        NetClock::time_point now)
{
    auto& ledgerMaster = app_.getLedgerMaster();
    auto const &prevLedger = prevLedgerT.hackAccess();
    // Tell the ledger master not to acquire the ledger we're probably building
    ledgerMaster.setBuildingLedger (prevLedger->info().seq + 1);

    ledgerMaster.applyHeldTransactions ();
    auto initialLedger = app_.openLedger().current();

    auto initialSet = std::make_shared <SHAMap> (
        SHAMapType::TRANSACTION, app_.family(), SHAMap::version{1});
    initialSet->setUnbacked ();

    // Build SHAMap containing all transactions in our open ledger
    for (auto const& tx : initialLedger->txs)
    {
        Serializer s (2048);
        tx.first->add(s);
        initialSet->addItem (
            SHAMapItem (tx.first->getTransactionID(), std::move (s)), true, false);
    }

    // Add pseudo-transactions to the set
    if ((app_.config().standalone() || (proposing && correctLCL))
            && ((prevLedger->info().seq % 256) == 0))
    {
        // previous ledger was flag ledger, add pseudo-transactions
        auto const validations =
            app_.getValidations().getValidations (
                prevLedger->info().parentHash);

        auto const count = std::count_if (
            validations.begin(), validations.end(),
            [](auto const& v)
            {
                return v.second->isTrusted();
            });

        if (count >= ledgerMaster.getMinValidations())
        {
            feeVote_.doVoting (
                prevLedger,
                validations,
                initialSet);
            app_.getAmendmentTable ().doVoting (
                prevLedger,
                validations,
                initialSet);
        }
    }

    // Now we need an immutable snapshot
    initialSet = initialSet->snapShot(false);
    auto setHash = initialSet->getHash().as_uint256();

    return std::make_pair<RCLTxSet, RCLCxPos> (
        std::move (initialSet),
        LedgerProposal {
            initialLedger->info().parentHash,
            setHash,
            closeTime,
            now});
}

RCLCxLedger RCLCxCalls::acquireLedger(LedgerHash const & ledgerHash)
{

    // we need to switch the ledger we're working from
    auto buildLCL = ledgerMaster_.getLedgerByHash(ledgerHash);
    if (! buildLCL)
    {
        if (acquiringLedger_ != ledgerHash)
        {
            // need to start acquiring the correct consensus LCL
            JLOG (j_.warn()) <<
                "Need consensus ledger " << ledgerHash;

            // Tell the ledger acquire system that we need the consensus ledger
            acquiringLedger_ = ledgerHash;

            auto app = &app_;
            auto hash = acquiringLedger_;
            app_.getJobQueue().addJob (
                jtADVANCE, "getConsensusLedger",
                [app, hash] (Job&) {
                    app->getInboundLedgers().acquire(
                        hash, 0, InboundLedger::fcCONSENSUS);
                });
        }
        return RCLCxLedger{};
    }

    assert (!buildLCL->open() && buildLCL->isImmutable ());
    assert (buildLCL->info().hash == ledgerHash);

    return RCLCxLedger(buildLCL);
}

void RCLCxCalls::statusChange(
    ChangeType c,
    RCLCxLedger const & ledger,
    bool haveCorrectLCL)
{
    protocol::TMStatusChange s;

    if (!haveCorrectLCL)
        s.set_newevent (protocol::neLOST_SYNC);
    else
    {
        switch (c)
        {
        case ChangeType::Closing:
            s.set_newevent (protocol::neCLOSING_LEDGER);
            break;
        case ChangeType::Accepted:
            s.set_newevent (protocol::neACCEPTED_LEDGER);
            break;
        }
    }

    s.set_ledgerseq (ledger.seq());
    s.set_networktime (app_.timeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious(ledger.parentId().begin (),
        std::decay_t<decltype(ledger.parentId())>::bytes);
    s.set_ledgerhash (ledger.id().begin (),
        std::decay_t<decltype(ledger.id())>::bytes);

    std::uint32_t uMin, uMax;
    if (! ledgerMaster_.getFullValidatedRange (uMin, uMax))
    {
        uMin = 0;
        uMax = 0;
    }
    else
    {
        // Don't advertise ledgers we're not willing to serve
        std::uint32_t early = ledgerMaster_.getEarliestFetch ();
        if (uMin < early)
           uMin = early;
    }
    s.set_firstseq (uMin);
    s.set_lastseq (uMax);
    app_.overlay ().foreach (send_always (
        std::make_shared <Message> (
            s, protocol::mtSTATUS_CHANGE)));
    JLOG (j_.trace()) << "send status change to peer";
}


//------------------------------------------------------------------------------
/** Apply a set of transactions to a ledger

  Typically the txFilter is used to reject transactions
  that already got in the prior ledger

  @param set            set of transactions to apply
  @param view           ledger to apply to
  @param txFilter       callback, return false to reject txn
  @return               retriable transactions
*/

CanonicalTXSet
applyTransactions (
    Application& app,
    RCLTxSet const& cSet,
    OpenView& view,
    std::function<bool(uint256 const&)> txFilter)
{
    auto j = app.journal ("LedgerConsensus");

    auto& set = *(cSet.map());
    CanonicalTXSet retriableTxs (set.getHash().as_uint256());


    for (auto const& item : set)
    {
        if (! txFilter (item.key()))
            continue;

        // The transaction wan't filtered
        // Add it to the set to be tried in canonical order
        JLOG (j.debug()) <<
            "Processing candidate transaction: " << item.key();
        try
        {
            retriableTxs.insert (
                std::make_shared<STTx const>(SerialIter{item.slice()}));
        }
        catch (std::exception const&)
        {
            JLOG (j.warn()) << "Txn " << item.key() << " throws";
        }
    }

    bool certainRetry = true;
    // Attempt to apply all of the retriable transactions
    for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
    {
        JLOG (j.debug()) << "Pass: " << pass << " Txns: "
            << retriableTxs.size ()
            << (certainRetry ? " retriable" : " final");
        int changes = 0;

        auto it = retriableTxs.begin ();

        while (it != retriableTxs.end ())
        {
            try
            {
                switch (applyTransaction (app, view,
                    *it->second, certainRetry, tapNO_CHECK_SIGN, j))
                {
                case ApplyResult::Success:
                    it = retriableTxs.erase (it);
                    ++changes;
                    break;

                case ApplyResult::Fail:
                    it = retriableTxs.erase (it);
                    break;

                case ApplyResult::Retry:
                    ++it;
                }
            }
            catch (std::exception const&)
            {
                JLOG (j.warn())
                    << "Transaction throws";
                it = retriableTxs.erase (it);
            }
        }

        JLOG (j.debug()) << "Pass: "
            << pass << " finished " << changes << " changes";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            return retriableTxs;

        // Stop retriable passes
        if (!changes || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }

    // If there are any transactions left, we must have
    // tried them in at least one final pass
    assert (retriableTxs.empty() || !certainRetry);
    return retriableTxs;
}


RCLCxLedger RCLCxCalls::buildLastClosedLedger(
    RCLCxLedger const & previousLedger,
    RCLTxSet const & set,
    NetClock::time_point closeTime,
    bool closeTimeCorrect,
    NetClock::duration closeResolution,
    NetClock::time_point now,
    std::chrono::milliseconds roundTime,
    RCLCxRetryTxSet & retriableTxs
)
{
    auto replay = ledgerMaster_.releaseReplay();
    if (replay)
    {
        // replaying, use the time the ledger we're replaying closed
        closeTime = replay->closeTime_;
        closeTimeCorrect = ((replay->closeFlags_ & sLCF_NoConsensusTime) == 0);
    }

    JLOG (j_.debug())
        << "Report: TxSt = " << set.getID ()
        << ", close " << closeTime.time_since_epoch().count()
        << (closeTimeCorrect ? "" : "X");


    // Build the new last closed ledger
    auto buildLCL = std::make_shared<Ledger>(
        *previousLedger.hackAccess(), now);
    auto const v2_enabled = buildLCL->rules().enabled(featureSHAMapV2,
                                                    app_.config().features);
    auto v2_transition = false;
    if (v2_enabled && !buildLCL->stateMap().is_v2())
    {
        buildLCL->make_v2();
        v2_transition = true;
    }

    // Set up to write SHAMap changes to our database,
    //   perform updates, extract changes
    JLOG (j_.debug())
        << "Applying consensus set transactions to the"
        << " last closed ledger";

    {
        OpenView accum(&*buildLCL);
        assert(!accum.open());
        if (replay)
        {
            // Special case, we are replaying a ledger close
            for (auto& tx : replay->txns_)
                applyTransaction (app_, accum, *tx.second, false, tapNO_CHECK_SIGN, j_);
        }
        else
        {
            // Normal case, we are not replaying a ledger close
            retriableTxs.txs() = applyTransactions (app_, set, accum,
                [&buildLCL](uint256 const& txID)
                {
                    return ! buildLCL->txExists(txID);
                });
        }
        // Update fee computations.
        app_.getTxQ().processClosedLedger(app_, accum,
            roundTime > 5s);
        accum.apply(*buildLCL);
    }

    // retriableTxs will include any transactions that
    // made it into the consensus set but failed during application
    // to the ledger.

    buildLCL->updateSkipList ();

    {
        // Write the final version of all modified SHAMap
        // nodes to the node store to preserve the new LCL

        int asf = buildLCL->stateMap().flushDirty (
            hotACCOUNT_NODE, buildLCL->info().seq);
        int tmf = buildLCL->txMap().flushDirty (
            hotTRANSACTION_NODE, buildLCL->info().seq);
        JLOG (j_.debug()) << "Flushed " <<
            asf << " accounts and " <<
            tmf << " transaction nodes";
    }
    buildLCL->unshare();

    // Accept ledger
    buildLCL->setAccepted(closeTime, closeResolution,
                        closeTimeCorrect, app_.config());

    // And stash the ledger in the ledger master
    if (ledgerMaster_.storeLedger (buildLCL))
        JLOG (j_.debug())
            << "Consensus built ledger we already had";
    else if (app_.getInboundLedgers().find (buildLCL->info().hash))
        JLOG (j_.debug())
            << "Consensus built ledger we were acquiring";
    else
        JLOG (j_.debug())
            << "Consensus built new ledger";
    return RCLCxLedger{std::move(buildLCL)};


}

bool RCLCxCalls::shouldValidate(RCLCxLedger const & ledger)
{
    return  ledgerMaster_.isCompatible(*ledger.hackAccess(),
        app_.journal("LedgerConsensus").warn(),
        "Not validating");
}

void RCLCxCalls::validate(
    RCLCxLedger const & ledger,
    NetClock::time_point now,
    bool proposing)
{
    // Build validation
    auto v = std::make_shared<STValidation> (ledger.id(),
        consensus_.validationTimestamp(now),
        valPublic_, proposing);
    v->setFieldU32 (sfLedgerSequence, ledger.seq());

    // Add our load fee to the validation
    auto const& feeTrack = app_.getFeeTrack();
    std::uint32_t fee = std::max(
        feeTrack.getLocalFee(),
        feeTrack.getClusterFee());

    if (fee > feeTrack.getLoadBase())
        v->setFieldU32(sfLoadFee, fee);

    if (((ledger.seq() + 1) % 256) == 0)
    // next ledger is flag ledger
    {
        // Suggest fee changes and new features
        feeVote_.doValidation (ledger.hackAccess(), *v);
        app_.getAmendmentTable ().doValidation (ledger.hackAccess(), *v);
    }

    auto const signingHash = v->sign (valSecret_);
    v->setTrusted ();
    // suppress it if we receive it - FIXME: wrong suppression
    app_.getHashRouter ().addSuppression (signingHash);
    app_.getValidations ().addValidation (v, "local");
    consensus_.setLastValidation (v);
    Blob validation = v->getSigned ();
    protocol::TMValidation val;
    val.set_validation (&validation[0], validation.size ());
    // Send signed validation to all of our directly connected peers
    app_.overlay().send(val);
}

void RCLCxCalls::consensusBuilt(
    RCLCxLedger const & ledger,
    Json::Value && json)
{
    ledgerMaster_.consensusBuilt (ledger.hackAccess(), std::move(json));
}


void RCLCxCalls::createOpenLedger(
    RCLCxLedger const & closedLedger,
    RCLCxRetryTxSet & retriableTxs,
    bool anyDisputes)
{
    // Build new open ledger
    auto lock = make_lock(
        app_.getMasterMutex(), std::defer_lock);
    auto sl = make_lock(
        ledgerMaster_.peekMutex (), std::defer_lock);
    std::lock(lock, sl);

    auto const lastVal = ledgerMaster_.getValidatedLedger();
    boost::optional<Rules> rules;
    if (lastVal)
        rules.emplace(*lastVal);
    else
        rules.emplace();
    app_.openLedger().accept(app_, *rules,
        closedLedger.hackAccess(), localTxs_.getTxSet(), anyDisputes, retriableTxs.txs(), tapNONE,
            "consensus",
                [&](OpenView& view, beast::Journal j)
                {
                    // Stuff the ledger with transactions from the queue.
                    return app_.getTxQ().accept(app_, view);
                });
}

void RCLCxCalls::switchLCL(RCLCxLedger const & ledger)
{
    ledgerMaster_.switchLCL (ledger.hackAccess());

    // Do these need to exist?
    assert (ledgerMaster_.getClosedLedger()->info().hash == ledger.id());
    assert (app_.openLedger().current()->info().parentHash == ledger.id());
}

void RCLCxCalls::adjustCloseTime(std::chrono::duration<std::int32_t> offset)
{
    app_.timeKeeper().adjustCloseTime(offset);
}

void RCLCxCalls::endConsensus(bool correctLCL)
{
    app_.getOPs ().endConsensus (correctLCL);
}
beast::Journal RCLCxCalls::journal(std::string const & s) const
{
    return app_.journal(s);
}

bool RCLCxCalls::hasOpenTransactions() const
{
    return ! app_.openLedger().empty();
}

int RCLCxCalls::getProposersValidated(LedgerHash const & h) const
{
    return app_.getValidations().getTrustedValidationCount(h);
}

} // namespace ripple
