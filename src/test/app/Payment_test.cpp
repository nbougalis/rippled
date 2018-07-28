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
        testcase ("Payments: without 'PartialPayment' amendment");

        using namespace jtx;

        Env env (*this, supported_amendments() - featurePartialPayments);

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];

        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        env.fund (XRP(10000), alice, bob, carol, gw);
        env.trust (USD (1000), alice, bob, carol);
        env (pay (gw, alice, USD(500)));
        env (pay (gw, bob, USD(500)));
        env (pay (gw, carol, USD(500)));

        // Issue "PartialPayment" transactions both with and without the
        // tfPartialPayment flag. They should fail because the amendment
        // is NOT enabled:
        env (partial_pay (bob, alice, USD(50)), ter(temUNKNOWN));
        env (partial_pay (bob, carol, USD(50)), txflags(tfPartialPayment), ter(temUNKNOWN));

        // Issue "Payment" transactions both with and without the
        // tfPartialPayment flag. They should succeed because the amendment
        // is NOT enabled:
        env (pay(bob, carol, USD(50)));
        env (pay(bob, alice, USD(50)), txflags(tfPartialPayment));
    }

    void
    testWithPartialPaymentAmendment()
    {
        testcase ("Payments: with 'PartialPayment' amendment");

        using namespace jtx;

        Env env (*this, supported_amendments() | featurePartialPayments);

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];

        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        env.fund (XRP(10000), alice, bob, carol, gw);
        env.trust (USD (1000), alice, bob, carol);
        env (pay (gw, alice, USD(500)));
        env (pay (gw, bob, USD(500)));
        env (pay (gw, carol, USD(500)));

        // Issue "Payment" and "PartialPayment" transactions using the
        // tfPartialPayment flag. They should fail because the amendment is
        // enabled:
        env (        pay(bob, alice, USD(50)), txflags(tfPartialPayment), ter(temINVALID_FLAG));
        env (partial_pay(bob, carol, USD(50)), txflags(tfPartialPayment), ter(temINVALID_FLAG));

        // Issue "Payment" and "PartialPayment" transactions. They should both
        // succeed because the amendment is enabled.
        env (pay(bob, carol, USD(50)));
        env (partial_pay(carol, alice, USD(50)));
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
