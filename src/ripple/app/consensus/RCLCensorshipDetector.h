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

#ifndef RIPPLE_APP_CONSENSUS_RCLCENSORSHIPDETECTOR_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCENSORSHIPDETECTOR_H_INCLUDED

#include <ripple/shamap/SHAMap.h>
#include <algorithm>
#include <map>

namespace ripple {

/** Threshold for issuing initial warning about potential censorship */
constexpr unsigned int censorshipLowThreshold = 10;

/** Threshold for issuing second warning about potential censorship */
constexpr unsigned int censorshipHighThreshold = 20;

template <class TxID, class Sequence>
class RCLCensorshipDetector
{
private:
    std::map<TxID, Sequence> tracker;

public:
    RCLCensorshipDetector() = default;

    /** Perform censorship detection

        This function is called when the server is proposing and a consensus
        round that it participated in completed. It tracks how long transactions
        proposed by this server haven't been included in the resulting ledger.

        @param proposed The set of transactions that we initially proposed for
                        this round.
        @param accepted The set of transactions that the network agreed should
                        be included in the ledger being built.
        @param seq The sequence number of the ledger being built.
        @param pred A predicate invoked for every transaction we are currently
                    tracking, and which returns true for tracking entries that
                    should be removed.
                    The signature of the predicate must be:
                        bool pred(TxID const&, Sequence)
    */
    template <class Predicate>
    void operator()(
        std::vector<TxID> proposed,
        std::vector<TxID> accepted,
        Sequence seq,
        Predicate pred)
    {
        // Find all the entries that we proposed but which were not accepted:
        std::vector<TxID> missing;

        std::sort (proposed.begin(), proposed.end());
        std::sort (accepted.begin(), accepted.end());

        std::set_difference(
            proposed.begin(), proposed.end(),
            accepted.begin(), accepted.end(),
            std::inserter(missing, missing.begin()));

        // Eliminate all entries that were accepted:
        for (auto const &a : accepted)
            tracker.erase (a);

        // Detect censorship attempts and prune items that failed.
        prune(pred);

        for (auto const& m : missing)
            tracker.emplace(m, seq);
    }

    /** Removes all elements satisfying specific criteria from the tracker

        @param pred A predicate which returns true for tracking entries
               that should be removed.
               The signature of the predicate must be:
                   bool pred(TxID const&, Sequence)
    */
    template <class Predicate>
    void prune(Predicate pred)
    {
        auto t = tracker.begin();

        while (t != tracker.end())
        {
            if (pred(t->first, t->second))
                t = tracker.erase(t);
            else
                t = std::next(t);
        }
    }

    /** Removes all elements from the tracker

        Typically, this function might be called after we reconnect to the
        network following an outage, or after we start tracking the network.
     */
    void reset()
    {
        tracker.clear();
    }
};

}

#endif
