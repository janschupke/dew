#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/RenderJob.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

struct ScratchFile
{
    ScratchFile()
        : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("dew-job-" + juce::Uuid().toString() + ".wav"))
    {
    }

    ~ScratchFile() { file.deleteFile(); }

    juce::File file;
};

RenderJob::Request requestFor (const juce::File& destination)
{
    RenderJob::Request request;
    request.project = dew::testing::fixtureProject();
    request.destination = destination;
    request.options.seconds = 0.5;

    return request;
}

/** Drives the job to completion without a message loop to pump. */
RenderReport runToCompletion (RenderJob& job, RenderJob::Request request)
{
    RenderReport result;
    bool called = false;

    REQUIRE (job.start (std::move (request), [&] (const RenderReport& r)
    {
        result = r;
        called = true;
    }));

    const auto deadline = juce::Time::getMillisecondCounter() + 30000;

    while (! called && juce::Time::getMillisecondCounter() < deadline)
    {
        job.poll();
        juce::Thread::sleep (5);
    }

    REQUIRE (called);
    return result;
}

} // namespace

TEST_CASE ("a job renders the same file the synchronous path does", "[engine][job]")
{
    ScratchFile viaJob, direct;

    RenderJob job;
    const auto asynchronous = runToCompletion (job, requestFor (viaJob.file));

    RenderOptions options;
    options.seconds = 0.5;
    const auto synchronous = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                             direct.file, options);

    INFO (asynchronous.result.getErrorMessage());
    REQUIRE (asynchronous.ok());
    REQUIRE (synchronous.ok());

    REQUIRE (asynchronous.numSamples == synchronous.numSamples);
    REQUIRE (asynchronous.peak == Approx (synchronous.peak));

    // The same render, so the same bytes.
    REQUIRE (viaJob.file.getSize() == direct.file.getSize());
    REQUIRE (viaJob.file.loadFileAsString() == direct.file.loadFileAsString());
}

TEST_CASE ("a job reports its result on the thread that polls it", "[engine][job]")
{
    ScratchFile scratch;

    RenderJob job;
    const auto report = runToCompletion (job, requestFor (scratch.file));

    REQUIRE (report.ok());
    REQUIRE_FALSE (job.isRunning());
    REQUIRE (job.getProgress() == Approx (1.0));
    REQUIRE (scratch.file.existsAsFile());
}

TEST_CASE ("only one render runs at a time", "[engine][job]")
{
    ScratchFile first, second;

    RenderJob job;

    RenderReport report;
    bool called = false;

    REQUIRE (job.start (requestFor (first.file), [&] (const RenderReport& r)
    {
        report = r;
        called = true;
    }));

    // Starting a second while the first is going is refused, not queued.
    REQUIRE_FALSE (job.start (requestFor (second.file), [] (const RenderReport&) {}));

    const auto deadline = juce::Time::getMillisecondCounter() + 30000;

    while (! called && juce::Time::getMillisecondCounter() < deadline)
    {
        job.poll();
        juce::Thread::sleep (5);
    }

    REQUIRE (called);
    REQUIRE (report.ok());
    REQUIRE_FALSE (second.file.existsAsFile());
}

TEST_CASE ("a cancelled job says so, and leaves no file behind", "[engine][job]")
{
    ScratchFile scratch;

    RenderJob job;

    RenderReport report;
    bool called = false;

    auto request = requestFor (scratch.file);
    request.options.seconds = 30.0;   // long enough to still be going

    REQUIRE (job.start (std::move (request), [&] (const RenderReport& r)
    {
        report = r;
        called = true;
    }));

    job.cancel();

    const auto deadline = juce::Time::getMillisecondCounter() + 30000;

    while (! called && juce::Time::getMillisecondCounter() < deadline)
    {
        job.poll();
        juce::Thread::sleep (5);
    }

    REQUIRE (called);

    // Cancelling is not a failure, and it must not have written anything.
    REQUIRE (report.cancelled);
    REQUIRE (report.ok());
    REQUIRE_FALSE (scratch.file.existsAsFile());
}

TEST_CASE ("a job can be started again once it has finished", "[engine][job]")
{
    ScratchFile first, second;

    RenderJob job;

    REQUIRE (runToCompletion (job, requestFor (first.file)).ok());
    REQUIRE (runToCompletion (job, requestFor (second.file)).ok());

    REQUIRE (first.file.existsAsFile());
    REQUIRE (second.file.existsAsFile());
}

TEST_CASE ("destroying a running job does not leave the thread behind", "[engine][job]")
{
    ScratchFile scratch;

    {
        RenderJob job;

        auto request = requestFor (scratch.file);
        request.options.seconds = 30.0;

        REQUIRE (job.start (std::move (request), [] (const RenderReport&) {}));
    }
    // The destructor cancels and joins. Reaching here at all is the assertion;
    // a leaked thread would trip JUCE's own leak detector at shutdown.

    SUCCEED();
}
