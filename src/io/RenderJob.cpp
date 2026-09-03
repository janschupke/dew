#include "io/RenderJob.h"

namespace dew
{

namespace
{

/** Often enough that a progress bar moves smoothly, rarely enough that it is
    not a cost of its own.
*/
constexpr int pollIntervalMs = 50;

} // namespace

RenderJob::RenderJob()
    : juce::Thread ("dew render")
{
}

RenderJob::~RenderJob()
{
    cancel();
    stopThread (shutdownTimeoutMs);
}

bool RenderJob::start (Request newRequest, std::function<void (const RenderReport&)> callback)
{
    if (isThreadRunning())
        return false;

    // A deep copy, because the message thread owns the tree and the user carries
    // on editing while this runs.
    request = std::move (newRequest);
    request.project = request.project.createCopy();

    onFinished = std::move (callback);

    report = {};
    finished.store (false);
    progress.fraction.store (0.0);
    progress.cancelled.store (false);
    progress.setStage ({});

    startThread();
    startTimer (pollIntervalMs);

    return true;
}

void RenderJob::cancel() noexcept
{
    progress.cancelled.store (true, std::memory_order_relaxed);
}

bool RenderJob::isRunning() const noexcept
{
    return isThreadRunning();
}

double RenderJob::getProgress() const noexcept
{
    return progress.fraction.load (std::memory_order_relaxed);
}

juce::String RenderJob::getStage() const
{
    return progress.getStage();
}

void RenderJob::run()
{
    report = request.stems ? OfflineRenderer::renderStems (request.project, request.destination,
                                                           request.options, &progress)
                           : OfflineRenderer::renderToFile (request.project, request.destination,
                                                            request.options, &progress);

    // Written last, so a reader that sees this has the report to go with it.
    finished.store (true, std::memory_order_release);
}

void RenderJob::timerCallback()
{
    poll();
}

void RenderJob::poll()
{
    if (! finished.load (std::memory_order_acquire) || isThreadRunning())
        return;

    stopTimer();

    // Cleared before the callback: it is allowed to start another render, and a
    // callback that outlives its own invocation is how that goes wrong.
    auto callback = std::exchange (onFinished, nullptr);
    finished.store (false);

    if (callback != nullptr)
        callback (report);
}

} // namespace dew
