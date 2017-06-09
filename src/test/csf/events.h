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
#ifndef RIPPLE_TEST_CSF_EVENTS_H_INCLUDED
#define RIPPLE_TEST_CSF_EVENTS_H_INCLUDED

#include <test/csf/Tx.h>
#include <test/csf/Validation.h>
#include <test/csf/ledgers.h>
#include <test/csf/Proposal.h>

#include <chrono>
namespace ripple {
namespace test {
namespace csf {

/** A value received from another node
 */
template <class V>
struct Receive
{
    //! Node that sent the value
    NodeID from;

    //! The received value
    V val;
};

/** A value sent to all other nodes
 */
template <class V>
struct Relay
{
    //! Event that is sent
    V val;
};

/** Node starts a new consensus round
 */
struct StartRound
{
    //! The preferred ledger for the start of consensus
    Ledger::ID bestLedger;

    //! The prior ledger on hand
    Ledger prevLedger;
};

/** Node closed the open ledger
 */
struct CloseLedger
{
    // The ledger closed on
    Ledger prevLedger;

    // Initial txs for including in ledger
    TxSetType txs;
};

//! Node accepted consensus results
struct AcceptLedger
{
    // The newly created ledger
    Ledger ledger;

    // The prior ledger (this is a jupm if prior.id() != ledger.parentID())
    Ledger priorLedger;
};

//! Node detected a wrong prior ledger during consensus
struct WrongPrevLedger
{
    // ID of wrong ledger we had
    Ledger::ID wrong;
    // ID of what we think is the correct ledger
    Ledger::ID right;
};

//! Node fully validated a new ledger
struct FullyValidateLedger
{
    //! The new fully validated ledger
    Ledger ledger;

    //! The prior fully validated ledger
    //! This is a jump if prior.id() != ledger.parentID()
    Ledger prior;
};

struct NullCollector
{
    template <class E>
    void
    on(NodeID node, std::chrono::steady_clock::time_point when, E const& e)
    {
    }
};

class Collector
{
    using tp = std::chrono::steady_clock::time_point;

    struct ICollector
    {
        virtual ~ICollector() = default;

        virtual std::unique_ptr<ICollector>
        copy() const = 0;

        virtual void
        on(NodeID node, tp when, Receive<Tx> const&) = 0;

        virtual void
        on(NodeID node, tp when, Receive<TxSet> const&) = 0;

        virtual void
        on(NodeID node, tp when, Receive<Validation> const&) = 0;

        virtual void
        on(NodeID node, tp when, Receive<Ledger> const&) = 0;

        virtual void
        on(NodeID node, tp when, Receive<Proposal> const&) = 0;

        virtual void
        on(NodeID node, tp when, Relay<Tx> const&) = 0;

        virtual void
        on(NodeID node, tp when, Relay<TxSet> const&) = 0;

        virtual void
        on(NodeID node, tp when, Relay<Validation> const&) = 0;

        virtual void
        on(NodeID node, tp when, Relay<Ledger> const&) = 0;

        virtual void
        on(NodeID node, tp when, Relay<Proposal> const&) = 0;

        virtual void
        on(NodeID node, tp when, StartRound const&) = 0;

        virtual void
        on(NodeID node, tp when, CloseLedger const&) = 0;

        virtual void
        on(NodeID node, tp when, AcceptLedger const&) = 0;

        virtual void
        on(NodeID node, tp when, WrongPrevLedger const&) = 0;

        virtual void
        on(NodeID node, tp when, FullyValidateLedger const&) = 0;
    };

    template <class T>
    class AnyCollector final : public ICollector
    {
        T t_;

    public:
        AnyCollector(T t) : t_{std::move(t)}
        {
        }

        std::unique_ptr<ICollector>
        copy() const override
        {
            return std::make_unique<AnyCollector>(*this);
        }
        void
        on(NodeID node, tp when, Receive<Tx> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Receive<TxSet> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Receive<Validation> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Receive<Ledger> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Receive<Proposal> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Relay<Tx> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Relay<TxSet> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Relay<Validation> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Relay<Ledger> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, Relay<Proposal> const& e) override
        {
            t_.on(node, when, e);
        }


        virtual void
        on(NodeID node, tp when, StartRound const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, CloseLedger const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, AcceptLedger const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, WrongPrevLedger const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(NodeID node, tp when, FullyValidateLedger const& e) override
        {
            t_.on(node, when, e);
        }
    };

    std::unique_ptr<ICollector> impl_;

public:
    template <class T>
    Collector(T t) : impl_{new AnyCollector<T>(std::forward<T>(t))}
    {
    }

    Collector(Collector const& c) = delete;

    Collector(Collector&&) = default;

    Collector&
    operator=(Collector& c) = delete;

    Collector&
    operator=(Collector&&) = default;

    template <class E>
    void
    on(NodeID node, tp when, E const& e)
    {
        impl_->on(node, when, e);
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
