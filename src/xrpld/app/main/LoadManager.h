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

#ifndef RIPPLE_APP_MAIN_LOADMANAGER_H_INCLUDED
#define RIPPLE_APP_MAIN_LOADMANAGER_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <atomic>
#include <memory>
#include <thread>

namespace ripple {

class Application;

/** Monitors server health and manages load-based local fee escalation.

    LoadManager runs a dedicated background thread that wakes approximately
    once per second to check whether the JobQueue is overloaded, and adjust
    the server's local fee.

    It also checks the application's watchdog, to track how long the server
    has gone without making forward progress. A sustained stall gets logged
    with escalating severity, and if the stall persists, we assume that the
    server is deadlocked and deliberately terminate it via LogicError.

    The background thread is created when the @ref start method is invoked,
    and is stopped when the @ref stop method is called (either directly or
    automatically by the destructor).
*/
class LoadManager
{
public:
    LoadManager(Application& app, beast::Journal journal);

    LoadManager() = delete;
    LoadManager(LoadManager const&) = delete;
    LoadManager&
    operator=(LoadManager const&) = delete;

    /** Destroy the manager.

        The destructor returns only after the thread has stopped.
    */
    ~LoadManager();

    //--------------------------------------------------------------------------

    /** Instruct the load manager to start background operations.

        Starting the load manager when it is already running has
        no effect.
     */
    void
    start();

    /** Instruct the load manager to stop background operations.

        Returns only after the background thread has exited; one final
        tick of work may still execute after stop is invoked.

        This may be called multiple times, but is not safe to call
        concurrently with itself or with start().

        This is called automatically by the destructor, but manually
        stopping allows fine-grained control.
     */
    void
    stop();

private:
    Application& app_;
    beast::Journal const journal_;

    std::atomic<bool> run_ = false;
    std::thread thread_;
};

}  // namespace ripple

#endif
