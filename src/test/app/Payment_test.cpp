//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/ApplyViewImpl.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/ledger/Sandbox.h>
#include <test/jtx/PathSet.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {
namespace test {

struct Payment_test : public beast::unit_test::suite
{
    void
    testWithoutPartialPaymentAmendment()
    {
        testcase ("Payments: without 'BlockPartialPayments' amendment");

        using namespace jtx;

        Env env (*this, supported_amendments() - featureBlockPartialPayments);

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        Account const alice ("alice");
        Account const bob ("bob");

        env.fund (XRP(10000), alice, bob, gw);
        env.trust (USD (5000), alice, bob);
        env.trust (EUR (5000), alice, bob);

        env (pay (gw, alice, USD(1500)));
        env (pay (gw, bob, USD(1500)));
        env (pay (gw, bob, EUR(1500)));

        // Use Bob as a source of liquidity for cross-currency payments:
        env (offer (bob, USD(500), EUR(500)));

        env (pay(bob, alice, USD(50)), txflags(tfPartialPayment));
        env (pay(bob, alice, USD(50)));
        env (pay(alice, alice, EUR (60)), sendmax (USD (50)), txflags (tfPartialPayment));
    }

    void
    testWithPartialPaymentAmendment()
    {
        testcase ("Payments: with 'BlockPartialPayments' amendment");

        using namespace jtx;

        Env env (*this, supported_amendments() | featureBlockPartialPayments);

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        env.fund (XRP(10000), alice, bob, carol, gw);
        env.trust (USD (5000), alice, bob, carol);
        env.trust (EUR (5000), alice, bob, carol);

        env (pay (gw, alice, USD(1500)));
        env (pay (gw, bob, USD(1500)));
        env (pay (gw, bob, EUR(1500)));
        env (pay (gw, carol, USD(1500)));

        // Use Bob as a source of liquidity for cross-currency payments:
        env (offer (bob, USD(500), EUR(500)));

        // Alice accepts partial payments from third parties but Carol doesn't.
        // Regardless of settings, partial payments to oneself must always work.
        env (fset(alice, asfPartialPayments),
             jtx::seq(jtx::autofill), fee(jtx::autofill), sig(jtx::autofill));
        env.require(nflags(alice, asfPartialPayments));

        env (pay(bob, alice, USD(50)), txflags(tfPartialPayment));
        env (pay(bob, alice, USD(50)));
        env (pay(alice, alice, EUR (60)), sendmax (USD (50)), txflags (tfPartialPayment));

        env (fclear(carol, asfPartialPayments),
             jtx::seq(jtx::autofill), fee(jtx::autofill), sig(jtx::autofill));
        env.require(flags(carol, asfPartialPayments));

        env (pay(bob, carol, USD(50)), txflags(tfPartialPayment), ter(tecNO_PARTIAL_PAYMENTS));
        env (pay(bob, carol, USD(50)));
        env (pay(carol, carol, EUR (60)), sendmax (USD (50)), txflags (tfPartialPayment));
    }

    void run() override
    {
        testWithPartialPaymentAmendment();
        testWithoutPartialPaymentAmendment();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Payment,app,ripple,2);

} // test
} // ripple
