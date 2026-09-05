#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <juce_events/juce_events.h>

namespace dew::control
{

/** Runs something on the message thread and waits for it to finish.

    Every operation touches the document, and a ValueTree is not thread safe, so
    a request that arrives on a socket has to cross over. There is no such
    helper anywhere else in dew - nothing had needed one, because the UI is
    already on the message thread and the render thread deliberately reports
    through a Timer rather than calling back.

    This is NOT the lock-free command queue realtime.md refuses. That refusal is
    about the AUDIO thread, whose contract is that it never waits and never
    allocates; nothing here is on it, and the thread doing the waiting is a
    socket thread whose entire job is to wait.

    ### The timeout is the point

    `callAsync` posts to a queue that a shut-down or modal-free message loop may
    never turn. Waiting forever would hang a socket thread on the way out of the
    application, which is a hang with no window on screen to explain it. So the
    wait is bounded and the answer is "no".

    The state is shared rather than captured by reference for the same reason: if
    the wait gives up, the posted lambda may still run later, and it must not
    write into a stack frame that has gone.

    ### And why it is also abandonable

    A timeout alone is not enough, because the message thread can be blocked on
    THIS thread finishing. Turning the endpoint off destroys the server from the
    message thread, which joins the socket thread with a two-second budget - and
    if that thread is here, waiting for a message only the joining thread could
    deliver, neither moves. The wait gives up after twenty seconds; JUCE gives
    up after two and kills the thread, mid-request, holding a socket.

    So the wait is sliced and `abandoned` is checked between slices. Whoever is
    tearing the caller down sets it before it starts, and this returns "no"
    while there is still time to unwind properly.

    @returns false if the message thread did not get to it in time, or if the
             caller was abandoned while waiting.
*/
template <typename Fn>
bool callOnMessageThread (Fn&& fn, int timeoutMs, const std::atomic<bool>& abandoned)
{
    // Already there - the ordinary case for a test, and for anything the
    // interface itself calls. Posting would deadlock: the message thread would
    // be waiting for a message only it can deliver.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        fn();
        return true;
    }

    struct Shared
    {
        juce::WaitableEvent done;
    };

    auto shared = std::make_shared<Shared>();

    juce::MessageManager::callAsync (
        [shared, work = std::function<void()> (std::forward<Fn> (fn))]
        {
            work();
            shared->done.signal();
        });

    // Short enough that a teardown waiting on this thread is not kept past its
    // own budget, long enough that an ordinary hop does not spin.
    constexpr int sliceMs = 25;

    for (int waited = 0; waited < timeoutMs; waited += sliceMs)
    {
        if (shared->done.wait (juce::jmin (sliceMs, timeoutMs - waited)))
            return true;

        // AFTER the wait, not before: a caller that was abandoned while the
        // work was already running should still see it finish.
        if (abandoned.load (std::memory_order_relaxed))
            return false;
    }

    return false;
}

} // namespace dew::control
