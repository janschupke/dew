#pragma once

#include "io/OfflineRenderer.h"

namespace dew
{

/** One render, running off the message thread.

    OfflineRenderer stays synchronous and stateless - being callable from
    anywhere with no state of its own is what makes it testable, and what
    dew_render and the render tests depend on. The thread lives here instead, in
    one file the engine itself never sees. It is the only thread dew has.

    A background thread is not a nicety for the MP3 path, it is a requirement:
    LAMEEncoderAudioFormat runs the encoder from its writer's DESTRUCTOR and
    blocks reading the child process's output until it exits. On the message
    thread that is a frozen window, and no amount of slicing the render into
    timer callbacks would avoid it.

    Completion is reported on the message thread by construction: the timer sees
    the thread has finished and calls back from its own callback. There is no
    callAsync, no cross-thread call into a Component, and no question about what
    is alive when the answer arrives.
*/
class RenderJob : private juce::Thread,
                  private juce::Timer
{
public:
    struct Request
    {
        /** Deep-copied on start. ValueTree is not thread safe, and the user
            keeps editing while a render runs.
        */
        juce::ValueTree project;

        /** A file, or the folder to fill when `stems` is set. */
        juce::File destination;

        RenderOptions options;
        bool stems = false;
    };

    RenderJob();
    ~RenderJob() override;

    /** Message thread. Returns false if one is already going. */
    bool start (Request request, std::function<void (const RenderReport&)> onFinished);

    /** Any thread. Honoured between blocks - so not during an MP3 encode, which
        has no handle to interrupt.
    */
    void cancel() noexcept;

    bool isRunning() const noexcept;

    /** 0..1 across the whole job, stems included. */
    double getProgress() const noexcept;

    /** "Stem 3 of 12", or empty. */
    juce::String getStage() const;

    /** Delivers the result if the render has finished.

        The timer calls this while the app is running. A test with no message
        loop to pump can call it in a loop instead; it is idempotent and does
        nothing until there is something to report.
    */
    void poll();

    /** How long the destructor waits. Generous on purpose: once lame has been
        started it cannot be interrupted, and killing the thread underneath it
        leaves a half-written temporary and a stranded child process.
    */
    static constexpr int shutdownTimeoutMs = 60000;

private:
    void run() override;
    void timerCallback() override;

    Request request;
    RenderProgress progress;
    RenderReport report;
    std::function<void (const RenderReport&)> onFinished;
    std::atomic<bool> finished { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderJob)
};

} // namespace dew
