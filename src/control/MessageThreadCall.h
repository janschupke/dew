#pragma once

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

    @returns false if the message thread did not get to it in time.
*/
template <typename Fn> bool callOnMessageThread (Fn&& fn, int timeoutMs)
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

    return shared->done.wait (timeoutMs);
}

} // namespace dew::control
