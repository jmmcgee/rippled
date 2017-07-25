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
#ifndef RIPPLE_TEST_CSF_PEER_H_INCLUDED
#define RIPPLE_TEST_CSF_PEER_H_INCLUDED

#include <ripple/consensus/Consensus.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/consensus/Validations.h>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <algorithm>
#include <test/csf/CollectorRef.h>
#include <test/csf/Scheduler.h>
#include <test/csf/Tx.h>
#include <test/csf/TrustGraph.h>
#include <test/csf/Validation.h>
#include <test/csf/events.h>
#include <test/csf/ledgers.h>

namespace ripple {
namespace test {
namespace csf {

namespace bc = boost::container;

/** A single peer in the simulation.

    This is the main work-horse of the consensus simulation framework and is
    where many other components are integrated. The peer

     - Implements the Callbacks required by Consensus
     - Manages trust & network connections with other peers
     - Issues events back to the simulation based on its actions
     - Exposes most internal state for forcibly simulation unusual scenarios
*/
struct Peer
{
    /** Basic wrapper of a proposed position taken by a peer.

        For real consensus, this would add additional data for serialization
        and signing. For simulation, nothing extra is needed.
    */
    class Position
    {
    public:
        Position(Proposal const& p) : proposal_(p)
        {
        }

        Proposal const&
        proposal() const
        {
            return proposal_;
        }

        Json::Value
        getJson() const
        {
            return proposal_.getJson();
        }

    private:
        Proposal proposal_;
    };


    /** Simulated delays in internal peer processing.
     */
    struct ProcessingDelays
    {
        //! Delay in consensus calling doAccept to accepting and issuing
        //! validation
        std::chrono::milliseconds ledgerAccept{0};

        //! Delay in processing validations from remote peers
        std::chrono::milliseconds recvValidation{0};

        // Return the receive delay for message type M, default is no delay
        template <class M>
        SimDuration
        onReceive(M const&) const
        {
            return SimDuration{};
        }

        SimDuration
        onReceive(Validation const&) const
        {
            return recvValidation;
        }
    };

    /** Generic Validations policy that simply ignores recently stale validations
    */
    class StalePolicy
    {
        Peer& p_;

    public:
        StalePolicy(Peer& p) : p_{p}
        {
        }

        NetClock::time_point
        now() const
        {
            return p_.now();
        }

        void
        onStale(Validation&& v)
        {
        }

        void
        flush(hash_map<PeerKey, Validation>&& remaining)
        {
        }
    };

    /** Non-locking mutex to avoid locks in generic Validations
    */
    struct NotAMutex
    {
        void
        lock()
        {
        }

        void
        unlock()
        {
        }
    };


    //! Type definitions for generic consensus
    using Ledger_t = Ledger;
    using NodeID_t = PeerID;
    using TxSet_t = TxSet;
    using PeerPosition_t = Position;
    using Result = ConsensusResult<Peer>;

    //! Logging support that prefixes the peer ID
    beast::WrappedSink sink;
    beast::Journal j;

    //! Generic consensus
    Consensus<Peer> consensus;

    //! Our unique ID
    PeerID id;

    //! Current signing key
    PeerKey key;

    //! The oracle that manages unique ledgers
    LedgerOracle& oracle;

    //! Scheduler of events
    Scheduler& scheduler;

    //! Handle to network for sending messages
    BasicNetwork<Peer*>& net;

    //! Handle to Trust graph of network
    TrustGraph<Peer*>& trustGraph;

    //! openTxs that haven't been closed in a ledger yet
    TxSetType openTxs;

    //! The last ledger closed by this node
    Ledger lastClosedLedger;

    //! Ledgers this node has closed or loaded from the network
    hash_map<Ledger::ID, Ledger> ledgers;

    //! Validations from trusted nodes
    Validations<StalePolicy, Validation, NotAMutex> validations;
    using AddOutcome =
        Validations<StalePolicy, Validation, NotAMutex>::AddOutcome;

    //! The most recent ledger that has been fully validated by the network
    Ledger fullyValidatedLedger;

    //! Map from Ledger::ID to vector of Positions with that ledger
    //! as the prior ledger
    bc::flat_map<Ledger::ID, std::vector<Proposal>> peerPositions;
    //! TxSet associated with a TxSet::ID
    bc::flat_map<TxSet::ID, TxSet> txSets;

    // Ledgers and txSets that we have already attempted to acquire
    bc::flat_set<Ledger::ID> acquiringLedgers;
    bc::flat_set<TxSet::ID> acquiringTxSets;

    //! The number of ledgers this peer has completed
    int completedLedgers = 0;
    //! The number of ledges this peer should complete before stopping to run
    int targetLedgers = std::numeric_limits<int>::max();

    //! Skew samples from the network clock
    std::chrono::seconds clockSkew{0};

    //! Simulated delays to use for internal processing
    ProcessingDelays delays;

    //! Whether to simulate running as validator or just consensus observer
    bool runAsValidator = true;

    //TODO: Consider removing these two, they are only a convenience for tests
    // Number of proposers in the prior round
    std::size_t prevProposers = 0;
    // Duration of prior round
    std::chrono::milliseconds prevRoundTime;

    // Quorum of validations needed for a ledger to be fully validated
    // TODO: Use the logic in ValidatorList to set this
    std::size_t quorum = 0;

    // Simulation parameters
    ConsensusParms consensusParms;

    //! The collectors to report events to
    CollectorRefs & collectors;

    //! All peers start from the default constructed ledger
    Peer(
        std::uint32_t i,
        Scheduler& s,
        LedgerOracle& o,
        BasicNetwork<Peer*>& n,
        TrustGraph<Peer*> & tg,
        CollectorRefs & c,
        beast::Journal jIn)
        : sink(jIn, "Peer " + std::to_string(i) + ": ")
        , j(sink)
        , consensus(s.clock(), *this, j)
        , id{i}
        , key{id, 0}
        , oracle{o}
        , scheduler{s}
        , net{n}
        , trustGraph(tg)
        , validations{ValidationParms{}, s.clock(), j, *this}
        , collectors{c}
    {
        ledgers[lastClosedLedger.id()] = lastClosedLedger;

        // nodes always trust themselves . . SHOULD THEY?
        trustGraph.trust(this, this);
    }

    /**  Schedule the provided callback in `when` duration, but if
        `when` is 0, call immediately
    */
    template <class T>
    void
    schedule(std::chrono::nanoseconds when, T&& what)
    {
        using namespace std::chrono_literals;

        if (when == 0ns)
            what();
        else
            scheduler.in(when, std::forward<T>(what));
    }

    // Issue a new event to the collectors
    template <class E>
    void
    issue(E const & event)
    {
        // Use the scheduler time and not the peer's local time
        collectors.on(id, scheduler.now(), event);
    }

    //--------------------------------------------------------------------------
    // Trust and Network members
    void
    trust(Peer & o)
    {
        trustGraph.trust(this, &o);
    }

    void
    untrust(Peer & o)
    {
        trustGraph.untrust(this, &o);
    }

    bool
    trusts(Peer & o)
    {
        return trustGraph.trusts(this, &o);
    }

    bool
    trusts(PeerID const & oId)
    {
        for(auto const & p : trustGraph.trustedPeers(this))
            if(p->id == oId)
                return true;
        return false;
    }

    bool
    connect(Peer & o, SimDuration dur)
    {
        return net.connect(this, &o, dur);
    }

    bool
    disconnect(Peer & o)
    {
        return net.disconnect(this, &o);
    }

    //--------------------------------------------------------------------------
    // Generic Consensus members

    Ledger const*
    acquireLedger(Ledger::ID const& ledgerHash)
    {
        auto it = ledgers.find(ledgerHash);
        if (it != ledgers.end())
            return &(it->second);

        // Don't retry if we already are acquiring it
        if(!acquiringLedgers.emplace(ledgerHash).second)
            return nullptr;

        for (auto const& link : net.links(this))
        {
            // Send a messsage to neighbors to find the ledger
            net.send(
                this, link.to, [ to = link.to, from = this, ledgerHash ]() {
                    auto it = to->ledgers.find(ledgerHash);
                    if (it != to->ledgers.end())
                    {
                        // if the ledger is found, send it back to the original
                        // requesting peer where it is added to the available
                        // ledgers
                        to->net.send(to, from, [ from, ledger = it->second ]() {
                            from->ledgers.emplace(ledger.id(), ledger);
                        });
                    }
                });
        }
        return nullptr;
    }

    TxSet const*
    acquireTxSet(TxSet::ID const& setId)
    {
        auto it = txSets.find(setId);
        if (it != txSets.end())
            return &(it->second);

        // Don't retry if we already are acquiring it
        if(!acquiringTxSets.emplace(setId).second)
            return nullptr;

        for (auto const& link : net.links(this))
        {
            // Send a message to neighbors to find the tx set
            net.send(
                this, link.to, [ to = link.to, from = this, setId ]() {
                    auto it = to->txSets.find(setId);
                    if (it != to->txSets.end())
                    {
                        // If the txSet is found, send it back to the original
                        // requesting peer, where it is handled like a TxSet
                        // that was broadcast over the network
                        to->net.send(to, from, [ from, txSet = it->second ]() {
                            from->handle(txSet);
                        });
                    }
                });
        }
        return nullptr;
    }

    bool
    hasOpenTransactions() const
    {
        return !openTxs.empty();
    }

    std::size_t
    proposersValidated(Ledger::ID const& prevLedger)
    {
        return validations.numTrustedForLedger(prevLedger);
    }

    std::size_t
    proposersFinished(Ledger::ID const& prevLedger)
    {
        return validations.getNodesAfter(prevLedger);
    }

    Result
    onClose(
        Ledger const& prevLedger,
        NetClock::time_point closeTime,
        ConsensusMode mode)
    {
        issue(CloseLedger{prevLedger, openTxs});

        return Result(
            TxSet{openTxs},
            Proposal(
                prevLedger.id(),
                Proposal::seqJoin,
                TxSet::calcID(openTxs),
                closeTime,
                now(),
                id));
    }

    void
    onForceAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        ConsensusCloseTimes const& rawCloseTimes,
        ConsensusMode const& mode,
        Json::Value&& consensusJson)
    {
        onAccept(
            result,
            prevLedger,
            closeResolution,
            rawCloseTimes,
            mode,
            std::move(consensusJson));
    }

    void
    onAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        ConsensusCloseTimes const& rawCloseTimes,
        ConsensusMode const& mode,
        Json::Value&& consensusJson)
    {
        schedule(delays.ledgerAccept, [&]() {
            TxSet const acceptedTxs = injectTxs(prevLedger, result.set);
            auto newLedger = oracle.accept(
                prevLedger,
                acceptedTxs.txs(),
                closeResolution,
                result.position.closeTime());
            ledgers[newLedger.id()] = newLedger;

            issue(AcceptLedger{newLedger, lastClosedLedger});
            prevProposers = result.proposers;
            prevRoundTime = result.roundTime.read();
            lastClosedLedger = newLedger;

            auto it = std::remove_if(
                openTxs.begin(), openTxs.end(), [&](Tx const& tx) {
                    return acceptedTxs.exists(tx.id());
                });
            openTxs.erase(it, openTxs.end());

            if (runAsValidator)
            {
                Validation v{newLedger.id(),
                             newLedger.seq(),
                             now(),
                             now(),
                             key,
                             id,
                             false};
                // share is not trusted
                share(v);
                // we trust ourselves
                addTrustedValidation(v);
            }

            checkFullyValidated(newLedger);

            // kick off the next round...
            // in the actual implementation, this passes back through
            // network ops
            ++completedLedgers;
            // startRound sets the LCL state, so we need to call it once after
            // the last requested round completes
            if (completedLedgers <= targetLedgers)
            {
                startRound();
            }
        });
    }

    Ledger::Seq
    earliestAllowedSeq() const
    {
        if (lastClosedLedger.seq() > Ledger::Seq{20})
            return lastClosedLedger.seq() - 20;
        return Ledger::Seq{0};
    }
    Ledger::ID
    getPrevLedger(
        Ledger::ID const& ledgerID,
        Ledger const& ledger,
        ConsensusMode mode)
    {
        // only do if we are past the genesis ledger
        if (ledger.seq() == Ledger::Seq{0})
            return ledgerID;

        Ledger::ID parentID;
        // Only set the parent ID if we believe ledger is the right ledger
        if (mode != ConsensusMode::wrongLedger)
            parentID = ledger.parentID();

        // Get validators that are on our ledger, or "close" to being on
        // our ledger.
        auto ledgerCounts = validations.currentTrustedDistribution(
            ledgerID, parentID, earliestAllowedSeq());

        Ledger::ID netLgr = getPreferredLedger(ledgerID, ledgerCounts);

        if (netLgr != ledgerID)
        {
            issue(WrongPrevLedger{ledgerID, netLgr});
        }
        return netLgr;
    }

    void
    propose(Proposal const& pos)
    {
        share(pos);
    }

    ConsensusParms const&
    parms() const
    {
        return consensusParms;
    }

    // Not interested in tracking consensus mode changes
    void
    onModeChange(ConsensusMode, ConsensusMode)
    {
    }

    // Share a message by broadcasting to all connected peers
    template <class M>
    void
    share(M const& m)
    {
        issue(Share<M>{m});
        send(BroadcastMesg<M>{m,router.nextSeq++, this->id}, this->id);
    }

    // Unwrap the Position and share the raw proposal
    void
    share(Position const & p)
    {
        share(p.proposal());
    }

    //--------------------------------------------------------------------------
    // Validation members

    /** Add a trusted validation and return true if it is worth forwarding */
    bool
    addTrustedValidation(Validation v)
    {
        v.setTrusted();
        v.setSeen(now());
        AddOutcome res = validations.add(v.key(), v);

        if(res == AddOutcome::stale || res == AddOutcome::repeat)
            return false;

        // Acquire will try to get from network if not already local
        Ledger const* lgr = acquireLedger(v.ledgerID());
        if (lgr)
            checkFullyValidated(*lgr);
        return true;
    }

    /** Check if a new ledger can be deemed fully validated */
    void
    checkFullyValidated(Ledger const& ledger)
    {
        // Only consider ledgers newer than our last fully validated ledger
        if (ledger.seq() <= fullyValidatedLedger.seq())
            return;

        auto count = validations.numTrustedForLedger(ledger.id());
        auto numTrustedPeers = trustGraph.graph().outDegree(this);
        quorum = static_cast<std::size_t>(std::ceil(numTrustedPeers * 0.8));
        if (count >= quorum)
        {
            issue(FullyValidateLedger{ledger, fullyValidatedLedger});
            fullyValidatedLedger = ledger;
        }
    }

    //-------------------------------------------------------------------------
    // Peer messaging members

    // Basic Sequence number router
    //   A message that will be flooded across the network is tagged with a
    //   seqeuence number by the origin node in a BroadcastMesg. Receivers will
    //   ignore a message as stale if they've already processed a newer sequence
    //   number,  or wil process and potentially relay the message along.
    //
    //  The various bool handle(MessageType) members do the actual processing
    //  and should return true if the message should continue to be sent to peers.
    //
    //  WARN: This assumes messages are received and processed in the order they
    //        are sent, so that a peer receives a message with seq 1 from node 0
    //        before seq 2 from node 0, etc.
    //  TODO: Break this out into a class and identify type interface to allow
    //        alternate routing strategies
    template <class M>
    struct BroadcastMesg
    {
        M mesg;
        std::size_t seq;
        PeerID origin;
    };

    struct Router
    {
        std::size_t nextSeq = 1;
        bc::flat_map<PeerID, std::size_t> lastObservedSeq;
    };

    Router router;

    // Send a broadcast message to all peers
    template <class M>
    void
    send(BroadcastMesg<M> const& bm, PeerID from)
    {
        for (auto const& link : net.links(this))
        {
            if (link.to->id != from && link.to->id != bm.origin)
            {
                // cheat and don't bother sending if we know it has already been
                // used on the other end
                if (link.to->router.lastObservedSeq[bm.origin] < bm.seq)
                {
                    issue(Relay<M>{link.to->id, bm.mesg});
                    net.send(
                        this, link.to, [ to = link.to, bm, id = this->id ] {
                            to->receive(bm, id);
                        });
                }
            }
        }
    }

    // Receive a shared message, process it and consider continuing to relay it
    template <class M>
    void
    receive(BroadcastMesg<M> const& bm, PeerID from)
    {
        issue(Receive<M>{from, bm.mesg});
        if (router.lastObservedSeq[bm.origin] < bm.seq)
        {
            router.lastObservedSeq[bm.origin] = bm.seq;
            schedule(delays.onReceive(bm.mesg), [this, bm, from]
            {
                if (handle(bm.mesg))
                    send(bm, from);
            });
        }
    }

    // Type specific receive handlers, return true if the message should
    // continue to be broadcast to peers
    bool
    handle(Proposal const& p)
    {
        // Only relay untrusted proposals on the same ledger
        if(!trusts(p.nodeID()))
            return p.prevLedger() == lastClosedLedger.id();

        // TODO: This always suppresses relay of peer positions already seen
        // Should it allow forwarding if for a recent ledger ?
        auto& dest = peerPositions[p.prevLedger()];
        if (std::find(dest.begin(), dest.end(), p) != dest.end())
            return false;

        dest.push_back(p);

        // Rely on consensus to decide whether to relaying immediately
        return consensus.peerProposal(now(), Position{p});
    }

    bool
    handle(TxSet const& txs)
    {
        // save and map complete?
        auto it = txSets.insert(std::make_pair(txs.id(), txs));
        if (it.second)
            consensus.gotTxSet(now(), txs);
        // relay only if new
        return it.second;
    }

    bool
    handle(Tx const& tx)
    {
        // Ignore and suppress relay of tranasctions already in last ledger
        auto const& lastClosedTxs = lastClosedLedger.txs();
        if (lastClosedTxs.find(tx) != lastClosedTxs.end())
            return false;

        // only relay if it was new to our open ledger
        return openTxs.insert(tx).second;

    }

    bool
    handle(Validation const& v)
    {
        // TODO: This is not relaying untrusted validations
        if (!trusts(v.nodeID()))
            return false;

        // Will only relay if current
        return addTrustedValidation(v);
    }

    //  A locally submitted transaction
    void
    submit(Tx const& tx)
    {
        // Received this from ourselves
        issue(SubmitTx{tx});
        if(handle(tx))
            share(tx);
    }

    //--------------------------------------------------------------------------
    // Client/interaction members
    void
    timerEntry()
    {
        consensus.timerEntry(now());
        // only reschedule if not completed
        if (completedLedgers < targetLedgers)
            scheduler.in(parms().ledgerGRANULARITY, [&]() { timerEntry(); });
    }

    void
    startRound()
    {
        auto valDistribution = validations.currentTrustedDistribution(
            lastClosedLedger.id(),
            lastClosedLedger.parentID(),
            earliestAllowedSeq());

        Ledger::ID bestLCL =
            getPreferredLedger(lastClosedLedger.id(), valDistribution);

        issue(StartRound{bestLCL, lastClosedLedger});

        // TODO:
        //  - Get dominant peer ledger if no validated available?
        //  - Check that we are switching to something compatible with our
        //    (network) validated history of ledgers?
        consensus.startRound(
            now(), bestLCL, lastClosedLedger, runAsValidator);
    }

    void
    start()
    {
        // TODO: Expire validations less frequently
        validations.expire();
        scheduler.in(parms().ledgerGRANULARITY, [&]() { timerEntry(); });
        startRound();
    }

    NetClock::time_point
    now() const
    {
        // We don't care about the actual epochs, but do want the
        // generated NetClock time to be well past its epoch to ensure
        // any subtractions of two NetClock::time_point in the consensu
        // code are positive. (e.g. proposeFRESHNESS)
        using namespace std::chrono;
        using namespace std::chrono_literals;
        return NetClock::time_point(duration_cast<NetClock::duration>(
            scheduler.now().time_since_epoch() + 86400s + clockSkew));
    }


    Ledger::ID
    prevLedgerID() const
    {
        return consensus.prevLedgerID();
    }

    //-------------------------------------------------------------------------
    // Injects a specific transaction when generating the ledger following
    // the provided sequence.  This allows simulating a byzantine failure in
    // which a node generates the wrong ledger, even when consensus worked
    // properly.
    hash_map<Ledger::Seq, Tx> txInjections;

    TxSet
    injectTxs(Ledger prevLedger, TxSet const & src)
    {
        auto const it = txInjections.find(prevLedger.seq());

        if(it == txInjections.end())
            return src;
        TxSetType res{src.txs()};
        res.insert(it->second);

        return TxSet{res};

    }
};

/** A group of simulation Peers

    A PeerGroup is a convenient handle for logically grouping peers together,
    and then creating trust or network relations for the group at large. Peer
    groups may also be combined to build out more complex structures.
*/
class PeerGroup
{
    using peers_type = std::vector<Peer*>;
    peers_type peers_;
public:
    using iterator = peers_type::iterator;
    using const_iterator = peers_type::const_iterator;
    using reference = peers_type::reference;
    using const_reference = peers_type::const_reference;

    PeerGroup() = default;
    PeerGroup(PeerGroup const&) = default;
    PeerGroup(Peer* peer) : peers_{1, peer}
    {
    }
    PeerGroup(std::vector<Peer*>&& peers) : peers_{std::move(peers)}
    {
        std::sort(peers_.begin(), peers_.end());
    }
    PeerGroup(std::vector<Peer*> const& peers) : peers_{peers}
    {
        std::sort(peers_.begin(), peers_.end());
    }

    PeerGroup(std::set<Peer*> const& peers) : peers_{peers.begin(), peers.end()}
    {

    }

    iterator
    begin()
    {
        return peers_.begin();
    }

    iterator
    end()
    {
        return peers_.end();
    }

    const_iterator
    begin() const
    {
        return peers_.begin();
    }

    const_iterator
    end() const
    {
        return peers_.end();
    }

    const_reference
    operator[](std::size_t i) const
    {
        return peers_[i];
    }

    bool
    exists(Peer const * p)
    {
        return std::find(peers_.begin(), peers_.end(), p) != peers_.end();
    }

    std::size_t
    size() const
    {
        return peers_.size();
    }

    void
    trust(PeerGroup const & o)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                p->trust(*target);
            }
        }
    }

    void
    untrust(PeerGroup const & o)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                p->untrust(*target);
            }
        }
    }

    void
    connect(PeerGroup const& o, SimDuration delay)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                // cannot send messages to self over network
                if(p != target)
                    p->connect(*target, delay);
            }
        }
    }

    void
    disconnect(PeerGroup const &o)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                p->disconnect(*target);
            }
        }
    }

    void
    trustAndConnect(PeerGroup const & o, SimDuration delay)
    {
        trust(o);
        connect(o, delay);
    }

    void
    connectFromTrust(SimDuration delay)
    {
        for (Peer * peer : peers_)
        {
            for (Peer * to : peer->trustGraph.trustedPeers(peer))
            {
                peer->connect(*to, delay);
            }
        }
    }

    friend
    PeerGroup
    operator+(PeerGroup const & a, PeerGroup const & b)
    {
        PeerGroup res;
        std::set_union(
            a.peers_.begin(),
            a.peers_.end(),
            b.peers_.begin(),
            b.peers_.end(),
            std::back_inserter(res.peers_));
        return res;
    }

    friend
    PeerGroup
    operator+(PeerGroup && a, PeerGroup const & b)
    {
        PeerGroup res{std::move(a.peers_)};
        std::set_union(
            a.peers_.begin(),
            a.peers_.end(),
            b.peers_.begin(),
            b.peers_.end(),
            std::back_inserter(res.peers_));
        return res;
    }

    friend
    PeerGroup
    operator-(PeerGroup const & a, PeerGroup const & b)
    {
        PeerGroup res;

        std::set_difference(
            a.peers_.begin(),
            a.peers_.end(),
            b.peers_.begin(),
            b.peers_.end(),
            std::back_inserter(res.peers_));

        return res;
    }

    friend std::ostream&
    operator<<(std::ostream& o, PeerGroup const& t)
    {
        o << "{";
        bool first = true;
        for (Peer const* p : t)
        {
            if(!first)
                o << ", ";
            first = false;
            o << p->id;
        }
        o << "}";
        return o;
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple
#endif

