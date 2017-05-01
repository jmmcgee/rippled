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
#include <ripple/app/consensus/RCLConsensus.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LocalTxs.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/tx/apply.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/make_lock.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/digest.h>
#include <ripple/shamap/SHAMap.h>

namespace ripple {

struct RCLConsensus::Impl
{
    struct Adaptor
    {
        //! Ledger type presented to Consensus
        using Ledger_t = RCLCxLedger;
        //! Peer identifier type used in Consensus
        using NodeID_t = NodeID;
        //! TxSet type presented to Consensus
        using TxSet_t = RCLTxSet;
        //! Peer Position type presented to Consensus
        using PeerPosition_t = RCLCxPeerPos;

        using Result = ConsensusResult<Adaptor>;

        Adaptor(
            Application& app,
            std::unique_ptr<FeeVote>&& feeVote,
            LedgerMaster& ledgerMaster,
            LocalTxs& localTxs,
            InboundTransactions& inboundTransactions,
            beast::Journal journal)
            : app_(app)
            , feeVote_(std::move(feeVote))
            , ledgerMaster_(ledgerMaster)
            , localTxs_(localTxs)
            , inboundTransactions_{inboundTransactions}
            , j_(journal)
            , nodeID_{calcNodeID(app.nodeIdentity().first)}
        {
        }

        /** Attempt to acquire a specific ledger.

            If not available, asynchronously acquires from the network.

            @param ledger The ID/hash of the ledger acquire
            @return Optional ledger, will be seated if we locally had the ledger
        */
        boost::optional<RCLCxLedger>
        acquireLedger(LedgerHash const& ledger)
        {
            // we need to switch the ledger we're working from
            auto buildLCL = ledgerMaster_.getLedgerByHash(ledger);
            if (!buildLCL)
            {
                if (acquiringLedger_ != ledger)
                {
                    // need to start acquiring the correct consensus LCL
                    JLOG(j_.warn()) << "Need consensus ledger " << ledger;

                    // Tell the ledger acquire system that we need the consensus
                    // ledger
                    acquiringLedger_ = ledger;

                    auto app = &app_;
                    auto hash = acquiringLedger_;
                    app_.getJobQueue().addJob(
                        jtADVANCE, "getConsensusLedger", [app, hash](Job&) {
                            app->getInboundLedgers().acquire(
                                hash, 0, InboundLedger::fcCONSENSUS);
                        });
                }
                return boost::none;
            }

            assert(!buildLCL->open() && buildLCL->isImmutable());
            assert(buildLCL->info().hash == ledger);

            // Notify inbound transactions of the new ledger sequence number
            inboundTransactions_.newRound(buildLCL->info().seq);

            return RCLCxLedger(buildLCL);
        }

        /** Relay the given proposal to all peers

            @param peerPos The peer position to relay.
            */
        void
        relay(RCLCxPeerPos const& peerPos)
        {
            protocol::TMProposeSet prop;

            auto const& proposal = peerPos.proposal();

            prop.set_proposeseq(proposal.proposeSeq());
            prop.set_closetime(proposal.closeTime().time_since_epoch().count());

            prop.set_currenttxhash(
                proposal.position().begin(), proposal.position().size());
            prop.set_previousledger(
                proposal.prevLedger().begin(), proposal.position().size());

            auto const pk = peerPos.publicKey().slice();
            prop.set_nodepubkey(pk.data(), pk.size());

            auto const sig = peerPos.signature();
            prop.set_signature(sig.data(), sig.size());

            app_.overlay().relay(prop, peerPos.suppressionID());
        }

        /** Relay disputed transacction to peers.

            Only relay if the provided transaction hasn't been shared recently.

            @param tx The disputed transaction to relay.
        */
        void
        relay(RCLCxTx const& tx)
        {
            // If we didn't relay this transaction recently, relay it to all
            // peers
            if (app_.getHashRouter().shouldRelay(tx.id()))
            {
                auto const slice = tx.tx_.slice();

                protocol::TMTransaction msg;
                msg.set_rawtransaction(slice.data(), slice.size());
                msg.set_status(protocol::tsNEW);
                msg.set_receivetimestamp(
                    app_.timeKeeper().now().time_since_epoch().count());
                app_.overlay().foreach (send_always(
                    std::make_shared<Message>(msg, protocol::mtTRANSACTION)));
            }
        }

        /** Acquire the transaction set associated with a proposal.

            If the transaction set is not available locally, will attempt
            acquire it
            from the network.

            @param setId The transaction set ID associated with the proposal
            @return Optional set of transactions, seated if available.
        */
        boost::optional<RCLTxSet>
        acquireTxSet(RCLTxSet::ID const& setId)
        {
            if (auto set = inboundTransactions_.getSet(setId, true))
            {
                return RCLTxSet{std::move(set)};
            }
            return boost::none;
        }

        /** Whether the open ledger has any transactions
            */
        bool
        hasOpenTransactions() const
        {
            return !app_.openLedger().empty();
        }

        /** Number of proposers that have vallidated the given ledger

            @param h The hash of the ledger of interest
            @return the number of proposers that validated a ledger
        */
        std::size_t
        proposersValidated(LedgerHash const& h) const
        {
            return app_.getValidations().getTrustedValidationCount(h);
        }

        /** Number of proposers that have validated a ledger descended from
            requested ledger.

            @param h The hash of the ledger of interest.
            @return The number of validating peers that have validated a ledger
                    succeeding the one provided.
        */
        std::size_t
        proposersFinished(LedgerHash const& h) const
        {
            return app_.getValidations().getNodesAfter(h);
        }

        /** Propose the given position to my peers.

            @param proposal Our proposed position
        */
        void
        propose(RCLCxPeerPos::Proposal const& proposal)
        {
            JLOG(j_.trace()) << "We propose: "
                             << (proposal.isBowOut()
                                     ? std::string("bowOut")
                                     : ripple::to_string(proposal.position()));

            protocol::TMProposeSet prop;

            prop.set_currenttxhash(
                proposal.position().begin(), proposal.position().size());
            prop.set_previousledger(
                proposal.prevLedger().begin(), proposal.position().size());
            prop.set_proposeseq(proposal.proposeSeq());
            prop.set_closetime(proposal.closeTime().time_since_epoch().count());

            prop.set_nodepubkey(valPublic_.data(), valPublic_.size());

            auto signingHash = sha512Half(
                HashPrefix::proposal,
                std::uint32_t(proposal.proposeSeq()),
                proposal.closeTime().time_since_epoch().count(),
                proposal.prevLedger(),
                proposal.position());

            auto sig = signDigest(valPublic_, valSecret_, signingHash);

            prop.set_signature(sig.data(), sig.size());

            app_.overlay().send(prop);
        }

        /** Relay the given tx set to peers.

            @param set The TxSet to share.
        */
        void
        relay(RCLTxSet const& set)
        {
            inboundTransactions_.giveSet(set.id(), set.map_, false);
        }

        /** Get the ID of the previous ledger/last closed ledger(LCL) on the
            network

            @param ledgerID ID of previous ledger used by consensus
            @param ledger Previous ledger consensus has available
            @param mode Current consensus mode
            @return The id of the last closed network

            @note ledgerID may not match ledger.id() if we haven't acquired
                    the ledger matching ledgerID from the network
            */
        uint256
        getPrevLedger(
            uint256 ledgerID,
            RCLCxLedger const& ledger,
            ConsensusMode mode)
        {
            uint256 parentID;
            // Only set the parent ID if we believe ledger is the right ledger
            if (mode != ConsensusMode::wrongLedger)
                parentID = ledger.parentID();

            // Get validators that are on our ledger, or "close" to being on
            // our ledger.
            auto vals = app_.getValidations().getCurrentValidations(
                ledgerID, parentID, ledgerMaster_.getValidLedgerIndex());

            uint256 netLgr = ledgerID;
            int netLgrCount = 0;
            for (auto& it : vals)
            {
                // Switch to ledger supported by more peers
                // Or stick with ours on a tie
                if ((it.second.first > netLgrCount) ||
                    ((it.second.first == netLgrCount) &&
                     (it.first == ledgerID)))
                {
                    netLgr = it.first;
                    netLgrCount = it.second.first;
                }
            }

            if (netLgr != ledgerID)
            {
                if (mode != ConsensusMode::wrongLedger)
                    app_.getOPs().consensusViewChange();

                if (auto stream = j_.debug())
                {
                    for (auto& it : vals)
                        stream << "V: " << it.first << ", " << it.second.first;
                }
            }

            return netLgr;
        }

        /** Close the open ledger and return initial consensus position.

            @param ledger the ledger we are changing to
            @param closeTime When consensus closed the ledger
            @param mode Current consensus mode
            @return Tentative consensus result
        */
        Result
        onClose(
            RCLCxLedger const& ledger,
            NetClock::time_point const& closeTime,
            ConsensusMode mode)
        {
            const bool wrongLCL = mode == ConsensusMode::wrongLedger;
            const bool proposing = mode == ConsensusMode::proposing;

            notify(protocol::neCLOSING_LEDGER, ledger, !wrongLCL);

            auto const& prevLedger = ledger.ledger_;

            ledgerMaster_.applyHeldTransactions();
            // Tell the ledger master not to acquire the ledger we're probably
            // building
            ledgerMaster_.setBuildingLedger(prevLedger->info().seq + 1);

            auto initialLedger = app_.openLedger().current();

            auto initialSet = std::make_shared<SHAMap>(
                SHAMapType::TRANSACTION, app_.family(), SHAMap::version{1});
            initialSet->setUnbacked();

            // Build SHAMap containing all transactions in our open ledger
            for (auto const& tx : initialLedger->txs)
            {
                Serializer s(2048);
                tx.first->add(s);
                initialSet->addItem(
                    SHAMapItem(tx.first->getTransactionID(), std::move(s)),
                    true,
                    false);
            }

            // Add pseudo-transactions to the set
            if ((app_.config().standalone() || (proposing && !wrongLCL)) &&
                ((prevLedger->info().seq % 256) == 0))
            {
                // previous ledger was flag ledger, add pseudo-transactions
                auto const validations = app_.getValidations().getValidations(
                    prevLedger->info().parentHash);

                std::size_t const count = std::count_if(
                    validations.begin(), validations.end(), [](auto const& v) {
                        return v.second->isTrusted();
                    });

                if (count >= app_.validators().quorum())
                {
                    feeVote_->doVoting(prevLedger, validations, initialSet);
                    app_.getAmendmentTable().doVoting(
                        prevLedger, validations, initialSet);
                }
            }

            // Now we need an immutable snapshot
            initialSet = initialSet->snapShot(false);
            auto setHash = initialSet->getHash().as_uint256();

            return Result{
                std::move(initialSet),
                RCLCxPeerPos::Proposal{initialLedger->info().parentHash,
                                       RCLCxPeerPos::Proposal::seqJoin,
                                       setHash,
                                       closeTime,
                                       app_.timeKeeper().closeTime(),
                                       nodeID_}};
        }

        /** Process the accepted ledger.

            Accepting a ledger may be expensive, so this function can dispatch
            that call to another thread if desired.

            @param result The result of consensus
            @param prevLedger The closed ledger consensus worked from
            @param closeResolution The resolution used in agreeing on an
            effective
                                    closeTiem
            @param rawCloseTimes The unrounded closetimes of ourself and our
            peers
            @param mode Our participating mode at the time consensus was
            declared
        */
        void
        onAccept(
            Result const& result,
            RCLCxLedger const& prevLedger,
            NetClock::duration const& closeResolution,
            ConsensusCloseTimes const& rawCloseTimes,
            ConsensusMode const& mode,
            Json::Value&& consensusJson)
        {
            app_.getJobQueue().addJob(jtACCEPT, "acceptLedger", [&](auto&) {
                // note that no lock is held inside this thread, which
                // is fine since once a ledger is accepted, consensus
                // will not touch any internal state until startRound is called
                doAccept(
                    result,
                    prevLedger,
                    closeResolution,
                    rawCloseTimes,
                    mode,
                    std::move(consensusJson));
                app_.getOPs().endConsensus();
            });
        }

        /** Process the accepted ledger that was a result of simulation/force
            accept.

            @ref onAccept
        */
        void
        onForceAccept(
            Result const& result,
            RCLCxLedger const& prevLedger,
            NetClock::duration const& closeResolution,
            ConsensusCloseTimes const& rawCloseTimes,
            ConsensusMode const& mode,
            Json::Value&& consensusJson)
        {
            doAccept(
                result,
                prevLedger,
                closeResolution,
                rawCloseTimes,
                mode,
                std::move(consensusJson));
        }

        //!---------------------------------------------------------------------
        // Additional supporting members (not directly required by Consensus)

        /** Notify peers of a consensus state change

            @param ne Event type for notification
            @param ledger The ledger at the time of the state change
            @param haveCorrectLCL Whether we believ we have the correct LCL.
        */
        void
        notify(
            protocol::NodeEvent ne,
            RCLCxLedger const& ledger,
            bool haveCorrectLCL)
        {
            protocol::TMStatusChange s;

            if (!haveCorrectLCL)
                s.set_newevent(protocol::neLOST_SYNC);
            else
                s.set_newevent(ne);

            s.set_ledgerseq(ledger.seq());
            s.set_networktime(
                app_.timeKeeper().now().time_since_epoch().count());
            s.set_ledgerhashprevious(
                ledger.parentID().begin(),
                std::decay_t<decltype(ledger.parentID())>::bytes);
            s.set_ledgerhash(
                ledger.id().begin(),
                std::decay_t<decltype(ledger.id())>::bytes);

            std::uint32_t uMin, uMax;
            if (!ledgerMaster_.getFullValidatedRange(uMin, uMax))
            {
                uMin = 0;
                uMax = 0;
            }
            else
            {
                // Don't advertise ledgers we're not willing to serve
                uMin = std::max(uMin, ledgerMaster_.getEarliestFetch());
            }
            s.set_firstseq(uMin);
            s.set_lastseq(uMax);
            app_.overlay().foreach (send_always(
                std::make_shared<Message>(s, protocol::mtSTATUS_CHANGE)));
            JLOG(j_.trace()) << "send status change to peer";
        }

        /** Accept a new ledger based on the given transactions.

            @ref onAccept
            */
        void
        doAccept(
            Result const& result,
            RCLCxLedger const& prevLedger,
            NetClock::duration closeResolution,
            ConsensusCloseTimes const& rawCloseTimes,
            ConsensusMode const& mode,
            Json::Value&& consensusJson)
        {
            bool closeTimeCorrect;

            const bool proposing = mode == ConsensusMode::proposing;
            const bool haveCorrectLCL = mode != ConsensusMode::wrongLedger;
            const bool consensusFail = result.state == ConsensusState::MovedOn;

            auto consensusCloseTime = result.position.closeTime();

            if (consensusCloseTime == NetClock::time_point{})
            {
                // We agreed to disagree on the close time
                consensusCloseTime = prevLedger.closeTime() + 1s;
                closeTimeCorrect = false;
            }
            else
            {
                // We agreed on a close time
                consensusCloseTime = effCloseTime(
                    consensusCloseTime,
                    closeResolution,
                    prevLedger.closeTime());
                closeTimeCorrect = true;
            }

            JLOG(j_.debug()) << "Report: Prop=" << (proposing ? "yes" : "no")
                             << " val=" << (validating_ ? "yes" : "no")
                             << " corLCL=" << (haveCorrectLCL ? "yes" : "no")
                             << " fail=" << (consensusFail ? "yes" : "no");
            JLOG(j_.debug()) << "Report: Prev = " << prevLedger.id() << ":"
                             << prevLedger.seq();

            //--------------------------------------------------------------------------
            // Put transactions into a deterministic, but unpredictable, order
            CanonicalTXSet retriableTxs{result.set.id()};

            auto sharedLCL = buildLCL(
                prevLedger,
                result.set,
                consensusCloseTime,
                closeTimeCorrect,
                closeResolution,
                result.roundTime.read(),
                retriableTxs);

            auto const newLCLHash = sharedLCL.id();
            JLOG(j_.debug())
                << "Report: NewL  = " << newLCLHash << ":" << sharedLCL.seq();

            // Tell directly connected peers that we have a new LCL
            notify(protocol::neACCEPTED_LEDGER, sharedLCL, haveCorrectLCL);

            if (validating_)
                validating_ = ledgerMaster_.isCompatible(
                    *sharedLCL.ledger_,
                    app_.journal("LedgerConsensus").warn(),
                    "Not validating");

            if (validating_ && !consensusFail)
            {
                validate(sharedLCL, proposing);
                JLOG(j_.info()) << "CNF Val " << newLCLHash;
            }
            else
                JLOG(j_.info()) << "CNF buildLCL " << newLCLHash;

            // See if we can accept a ledger as fully-validated
            ledgerMaster_.consensusBuilt(sharedLCL.ledger_, consensusJson);

            //-------------------------------------------------------------------------
            {
                // Apply disputed transactions that didn't get in
                //
                // The first crack of transactions to get into the new
                // open ledger goes to transactions proposed by a validator
                // we trust but not included in the consensus set.
                //
                // These are done first because they are the most likely
                // to receive agreement during consensus. They are also
                // ordered logically "sooner" than transactions not mentioned
                // in the previous consensus round.
                //
                bool anyDisputes = false;
                for (auto& it : result.disputes)
                {
                    if (!it.second.getOurVote())
                    {
                        // we voted NO
                        try
                        {
                            JLOG(j_.debug())
                                << "Test applying disputed transaction that did"
                                << " not get in";

                            SerialIter sit(it.second.tx().tx_.slice());
                            auto txn = std::make_shared<STTx const>(sit);
                            retriableTxs.insert(txn);

                            anyDisputes = true;
                        }
                        catch (std::exception const&)
                        {
                            JLOG(j_.debug())
                                << "Failed to apply transaction we voted NO on";
                        }
                    }
                }

                // Build new open ledger
                auto lock = make_lock(app_.getMasterMutex(), std::defer_lock);
                auto sl = make_lock(ledgerMaster_.peekMutex(), std::defer_lock);
                std::lock(lock, sl);

                auto const lastVal = ledgerMaster_.getValidatedLedger();
                boost::optional<Rules> rules;
                if (lastVal)
                    rules.emplace(*lastVal, app_.config().features);
                else
                    rules.emplace(app_.config().features);
                app_.openLedger().accept(
                    app_,
                    *rules,
                    sharedLCL.ledger_,
                    localTxs_.getTxSet(),
                    anyDisputes,
                    retriableTxs,
                    tapNONE,
                    "consensus",
                    [&](OpenView& view, beast::Journal j) {
                        // Stuff the ledger with transactions from the queue.
                        return app_.getTxQ().accept(app_, view);
                    });

                // Signal a potential fee change to subscribers after the open
                // ledger
                // is created
                app_.getOPs().reportFeeChange();
            }

            //-------------------------------------------------------------------------
            {
                ledgerMaster_.switchLCL(sharedLCL.ledger_);

                // Do these need to exist?
                assert(
                    ledgerMaster_.getClosedLedger()->info().hash ==
                    sharedLCL.id());
                assert(
                    app_.openLedger().current()->info().parentHash ==
                    sharedLCL.id());
            }

            //-------------------------------------------------------------------------
            // we entered the round with the network,
            // see how close our close time is to other node's
            //  close time reports, and update our clock.
            if ((mode == ConsensusMode::proposing ||
                 mode == ConsensusMode::observing) &&
                !consensusFail)
            {
                auto closeTime = rawCloseTimes.self;

                JLOG(j_.info())
                    << "We closed at " << closeTime.time_since_epoch().count();
                using usec64_t = std::chrono::duration<std::uint64_t>;
                usec64_t closeTotal = std::chrono::duration_cast<usec64_t>(
                    closeTime.time_since_epoch());
                int closeCount = 1;

                for (auto const& p : rawCloseTimes.peers)
                {
                    // FIXME: Use median, not average
                    JLOG(j_.info())
                        << std::to_string(p.second) << " time votes for "
                        << std::to_string(p.first.time_since_epoch().count());
                    closeCount += p.second;
                    closeTotal += std::chrono::duration_cast<usec64_t>(
                                      p.first.time_since_epoch()) *
                        p.second;
                }

                closeTotal += usec64_t(closeCount / 2);  // for round to nearest
                closeTotal /= closeCount;

                // Use signed times since we are subtracting
                using duration = std::chrono::duration<std::int32_t>;
                using time_point = std::chrono::time_point<NetClock, duration>;
                auto offset = time_point{closeTotal} -
                    std::chrono::time_point_cast<duration>(closeTime);
                JLOG(j_.info()) << "Our close offset is estimated at "
                                << offset.count() << " (" << closeCount << ")";

                app_.timeKeeper().adjustCloseTime(offset);
            }
        }

        /** Apply a set of transactions to a ledger.

          Typically the txFilter is used to reject transactions
          that already accepted in the prior ledger.

          @param set            set of transactions to apply
          @param view           ledger to apply to
          @param txFilter       callback, return false to reject txn
          @return               retriable transactions
        */
        static
        CanonicalTXSet
        applyTransactions(
            Application& app,
            RCLTxSet const& cSet,
            OpenView& view,
            std::function<bool(uint256 const&)> txFilter)
        {
            auto j = app.journal("LedgerConsensus");

            auto& set = *(cSet.map_);
            CanonicalTXSet retriableTxs(set.getHash().as_uint256());

            for (auto const& item : set)
            {
                if (!txFilter(item.key()))
                    continue;

                // The transaction wan't filtered
                // Add it to the set to be tried in canonical order
                JLOG(j.debug())
                    << "Processing candidate transaction: " << item.key();
                try
                {
                    retriableTxs.insert(
                        std::make_shared<STTx const>(SerialIter{item.slice()}));
                }
                catch (std::exception const&)
                {
                    JLOG(j.warn()) << "Txn " << item.key() << " throws";
                }
            }

            bool certainRetry = true;
            // Attempt to apply all of the retriable transactions
            for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
            {
                JLOG(j.debug())
                    << "Pass: " << pass << " Txns: " << retriableTxs.size()
                    << (certainRetry ? " retriable" : " final");
                int changes = 0;

                auto it = retriableTxs.begin();

                while (it != retriableTxs.end())
                {
                    try
                    {
                        switch (applyTransaction(
                            app,
                            view,
                            *it->second,
                            certainRetry,
                            tapNO_CHECK_SIGN,
                            j))
                        {
                            case ApplyResult::Success:
                                it = retriableTxs.erase(it);
                                ++changes;
                                break;

                            case ApplyResult::Fail:
                                it = retriableTxs.erase(it);
                                break;

                            case ApplyResult::Retry:
                                ++it;
                        }
                    }
                    catch (std::exception const&)
                    {
                        JLOG(j.warn()) << "Transaction throws";
                        it = retriableTxs.erase(it);
                    }
                }

                JLOG(j.debug()) << "Pass: " << pass << " finished " << changes
                                << " changes";

                // A non-retry pass made no changes
                if (!changes && !certainRetry)
                    return retriableTxs;

                // Stop retriable passes
                if (!changes || (pass >= LEDGER_RETRY_PASSES))
                    certainRetry = false;
            }

            // If there are any transactions left, we must have
            // tried them in at least one final pass
            assert(retriableTxs.empty() || !certainRetry);
            return retriableTxs;
        }

        /** Build the new last closed ledger.

            Accept the given the provided set of consensus transactions and
            build
            the last closed ledger. Since consensus just agrees on which
            transactions to apply, but not whether they make it into the closed
            ledger, this function also populates retriableTxs with those that
            can
            be retried in the next round.

            @param previousLedger Prior ledger building upon
            @param set The set of transactions to apply to the ledger
            @param closeTime The the ledger closed
            @param closeTimeCorrect Whether consensus agreed on close time
            @param closeResolution Resolution used to determine consensus close
            time
            @param roundTime Duration of this consensus rorund
            @param retriableTxs Populate with transactions to retry in next
            round
            @return The newly built ledger
        */
        RCLCxLedger
        buildLCL(
            RCLCxLedger const& previousLedger,
            RCLTxSet const& set,
            NetClock::time_point closeTime,
            bool closeTimeCorrect,
            NetClock::duration closeResolution,
            std::chrono::milliseconds roundTime,
            CanonicalTXSet& retriableTxs)
        {
            auto replay = ledgerMaster_.releaseReplay();
            if (replay)
            {
                // replaying, use the time the ledger we're replaying closed
                closeTime = replay->closeTime_;
                closeTimeCorrect =
                    ((replay->closeFlags_ & sLCF_NoConsensusTime) == 0);
            }

            JLOG(j_.debug()) << "Report: TxSt = " << set.id() << ", close "
                             << closeTime.time_since_epoch().count()
                             << (closeTimeCorrect ? "" : "X");

            // Build the new last closed ledger
            auto buildLCL =
                std::make_shared<Ledger>(*previousLedger.ledger_, closeTime);

            auto const v2_enabled = buildLCL->rules().enabled(featureSHAMapV2);

            auto v2_transition = false;
            if (v2_enabled && !buildLCL->stateMap().is_v2())
            {
                buildLCL->make_v2();
                v2_transition = true;
            }

            // Set up to write SHAMap changes to our database,
            //   perform updates, extract changes
            JLOG(j_.debug()) << "Applying consensus set transactions to the"
                             << " last closed ledger";

            {
                OpenView accum(&*buildLCL);
                assert(!accum.open());
                if (replay)
                {
                    // Special case, we are replaying a ledger close
                    for (auto& tx : replay->txns_)
                        applyTransaction(
                            app_,
                            accum,
                            *tx.second,
                            false,
                            tapNO_CHECK_SIGN,
                            j_);
                }
                else
                {
                    // Normal case, we are not replaying a ledger close
                    retriableTxs = applyTransactions(
                        app_, set, accum, [&buildLCL](uint256 const& txID) {
                            return !buildLCL->txExists(txID);
                        });
                }
                // Update fee computations.
                app_.getTxQ().processClosedLedger(app_, accum, roundTime > 5s);
                accum.apply(*buildLCL);
            }

            // retriableTxs will include any transactions that
            // made it into the consensus set but failed during application
            // to the ledger.

            buildLCL->updateSkipList();

            {
                // Write the final version of all modified SHAMap
                // nodes to the node store to preserve the new LCL

                int asf = buildLCL->stateMap().flushDirty(
                    hotACCOUNT_NODE, buildLCL->info().seq);
                int tmf = buildLCL->txMap().flushDirty(
                    hotTRANSACTION_NODE, buildLCL->info().seq);
                JLOG(j_.debug()) << "Flushed " << asf << " accounts and " << tmf
                                 << " transaction nodes";
            }
            buildLCL->unshare();

            // Accept ledger
            buildLCL->setAccepted(
                closeTime, closeResolution, closeTimeCorrect, app_.config());

            // And stash the ledger in the ledger master
            if (ledgerMaster_.storeLedger(buildLCL))
                JLOG(j_.debug()) << "Consensus built ledger we already had";
            else if (app_.getInboundLedgers().find(buildLCL->info().hash))
                JLOG(j_.debug()) << "Consensus built ledger we were acquiring";
            else
                JLOG(j_.debug()) << "Consensus built new ledger";
            return RCLCxLedger{std::move(buildLCL)};
        }

        /** Validate the given ledger and share with peers as necessary

            @param ledger The ledger to validate
            @param proposing Whether we were proposing transactions while
            generating
                                this ledger.  If we are not proposing, a
           validation
                                can still be sent to inform peers that we know
           we
                                aren't fully participating in consensus but are
            still
                                around and trying to catch up.
        */
        void
        validate(RCLCxLedger const& ledger, bool proposing)
        {
            auto validationTime = app_.timeKeeper().closeTime();
            if (validationTime <= lastValidationTime_)
                validationTime = lastValidationTime_ + 1s;
            lastValidationTime_ = validationTime;

            // Build validation
            auto v = std::make_shared<STValidation>(
                ledger.id(), validationTime, valPublic_, proposing);
            v->setFieldU32(sfLedgerSequence, ledger.seq());

            // Add our load fee to the validation
            auto const& feeTrack = app_.getFeeTrack();
            std::uint32_t fee =
                std::max(feeTrack.getLocalFee(), feeTrack.getClusterFee());

            if (fee > feeTrack.getLoadBase())
                v->setFieldU32(sfLoadFee, fee);

            if (((ledger.seq() + 1) % 256) == 0)
            // next ledger is flag ledger
            {
                // Suggest fee changes and new features
                feeVote_->doValidation(ledger.ledger_, *v);
                app_.getAmendmentTable().doValidation(ledger.ledger_, *v);
            }

            auto const signingHash = v->sign(valSecret_);
            v->setTrusted();
            // suppress it if we receive it - FIXME: wrong suppression
            app_.getHashRouter().addSuppression(signingHash);
            app_.getValidations().addValidation(v, "local");
            Blob validation = v->getSerialized();
            protocol::TMValidation val;
            val.set_validation(&validation[0], validation.size());
            // Send signed validation to all of our directly connected peers
            app_.overlay().send(val);
        }

        Application& app_;
        std::unique_ptr<FeeVote> feeVote_;
        LedgerMaster& ledgerMaster_;
        LocalTxs& localTxs_;
        InboundTransactions& inboundTransactions_;
        beast::Journal j_;

        NodeID nodeID_;
        PublicKey valPublic_;
        SecretKey valSecret_;
        LedgerHash acquiringLedger_;

        // The timestamp of the last validation we used, in network time. This
        // is
        // only used for our own validations.
        NetClock::time_point lastValidationTime_;

        bool validating_ = false;
    };

    Impl(
        Application& app,
        std::unique_ptr<FeeVote>&& feeVote,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs,
        InboundTransactions& inboundTransactions,
        Consensus<Impl::Adaptor>::clock_type const& clock,
        beast::Journal journal)
        : adaptor_(
              app,
              std::move(feeVote),
              ledgerMaster,
              localTxs,
              inboundTransactions,
              journal)
        , consensus_(clock, adaptor_, journal)
    {
    }

    Adaptor adaptor_;
    Consensus<Adaptor> consensus_;
};

RCLConsensus::RCLConsensus(
    Application& app,
    std::unique_ptr<FeeVote>&& feeVote,
    LedgerMaster& ledgerMaster,
    LocalTxs& localTxs,
    InboundTransactions& inboundTransactions,
    beast::abstract_clock<std::chrono::steady_clock> const& clock,
    beast::Journal journal)
    : impl_{ std::make_unique<Impl>(
          app,
          std::move(feeVote),
          ledgerMaster,
          localTxs,
          inboundTransactions,
          clock,
          journal) }
{

}

RCLConsensus::~RCLConsensus() = default;

Json::Value
RCLConsensus::getJson(bool full) const
{
    auto ret = impl_->consensus_.getJson(full);
    ret["validating"] = impl_->adaptor_.validating_;
    return ret;
}

PublicKey const&
RCLConsensus::getValidationPublicKey() const
{
    return impl_->adaptor_.valPublic_;
}

void
RCLConsensus::setValidationKeys(
    SecretKey const& valSecret,
    PublicKey const& valPublic)
{
    impl_->adaptor_.valSecret_ = valSecret;
    impl_->adaptor_.valPublic_ = valPublic;
}

void
RCLConsensus::timerEntry(NetClock::time_point const& now)
{
    try
    {
        impl_->consensus_.timerEntry(now);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        JLOG(impl_->adaptor_.j_.error())
            << "Missing node during consensus process " << mn;
        Rethrow();
    }
}

void
RCLConsensus::gotTxSet(NetClock::time_point const& now, RCLTxSet const& txSet)
{
    try
    {
        impl_->consensus_.gotTxSet(now, txSet);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        JLOG(impl_->adaptor_.j_.error())
            << "Missing node during consensus process " << mn;
        Rethrow();
    }
}

void
RCLConsensus::startRound(
    NetClock::time_point const& now,
    RCLCxLedger::ID const& prevLgrId,
    RCLCxLedger const& prevLgr)
{
    // We have a key, and we have some idea what the ledger is
    impl_->adaptor_.validating_ = !impl_->adaptor_.app_.getOPs().isNeedNetworkLedger() &&
        (impl_->adaptor_.valPublic_.size() != 0);

    // propose only if we're in sync with the network (and validating)
    bool proposing = impl_->adaptor_.validating_ &&
        (impl_->adaptor_.app_.getOPs().getOperatingMode() == NetworkOPs::omFULL);

    if (impl_->adaptor_.validating_)
    {
        JLOG(impl_->adaptor_.j_.info()) << "Entering consensus process, validating";
    }
    else
    {
        // Otherwise we just want to monitor the validation process.
        JLOG(impl_->adaptor_.j_.info()) << "Entering consensus process, watching";
    }

    // Notify inbOund ledgers that we are starting a new round
    impl_->adaptor_.inboundTransactions_.newRound(prevLgr.seq());

    impl_->consensus_.startRound(now, prevLgrId, prevLgr, proposing);
}

bool
RCLConsensus::validating() const
{
    return impl_->adaptor_.validating_;
}

bool
RCLConsensus::haveCorrectLCL() const
{
    return impl_->consensus_.mode() != ConsensusMode::wrongLedger;
}

bool
RCLConsensus::proposing() const
{
    return impl_->consensus_.mode() == ConsensusMode::proposing;
}

RCLCxLedger::ID
RCLConsensus::prevLedgerID() const
{
    return impl_->consensus_.prevLedgerID();
}


std::size_t
RCLConsensus::prevProposers() const
{
    return impl_->consensus_.prevProposers();
}

std::chrono::milliseconds
RCLConsensus::prevRoundTime() const
{
    return impl_->consensus_.prevRoundTime();
}

void
RCLConsensus::simulate(
    NetClock::time_point const& now,
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    impl_->consensus_.simulate(now, consensusDelay);
}

bool
RCLConsensus::peerProposal(
    NetClock::time_point const& now,
    RCLCxPeerPos const& newProposal)
{
    return impl_->consensus_.peerProposal(now, newProposal);
}

}
