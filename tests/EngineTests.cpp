#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

using namespace dew;
using Catch::Approx;

namespace
{

float peakOf (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
}

/** Peak within a time window, for checking that sound happens WHEN it should. */
float peakBetween (const juce::AudioBuffer<float>& buffer, double fromSeconds, double toSeconds,
                   double sampleRate)
{
    const auto start = juce::jlimit (0, buffer.getNumSamples(), (int) (fromSeconds * sampleRate));
    const auto end = juce::jlimit (0, buffer.getNumSamples(), (int) (toSeconds * sampleRate));

    return end > start ? buffer.getMagnitude (start, end - start) : 0.0f;
}

} // namespace

TEST_CASE ("rendering the demo project produces audio", "[engine][render]")
{
    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(), rendered);

    INFO ("warnings: " << report.warnings.joinIntoString ("; "));
    REQUIRE (report.ok());
    REQUIRE (report.warnings.isEmpty());

    REQUIRE (rendered.getNumChannels() == 2);
    REQUIRE (rendered.getNumSamples() > 0);

    // Audible, and not clipping.
    REQUIRE (report.peak > 0.05f);
    REQUIRE (report.peak <= 1.0f);
    REQUIRE (report.rms > 0.01f);

    // Nothing pathological: no NaNs or infinities anywhere.
    for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
    {
        const auto* data = rendered.getReadPointer (channel);

        for (int i = 0; i < rendered.getNumSamples(); ++i)
            REQUIRE (std::isfinite (data[i]));
    }
}

TEST_CASE ("the render is as long as the music", "[engine][render]")
{
    RenderOptions options;
    options.tailSeconds = 1.0;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(), rendered, options);

    REQUIRE (report.ok());

    // Four bars at 124 bpm = 16 beats = 16 * 60/124 seconds, plus the tail.
    const auto expected = 16.0 * 60.0 / 124.0 + 1.0;
    REQUIRE (report.seconds == Approx (expected).epsilon (0.01));
}

TEST_CASE ("the tail after the music is a decay, not the song starting again", "[engine][render]")
{
    // The transport loops, so without stopping it the "tail" would be bar one
    // playing again - loud, and wrong.
    RenderOptions options;
    options.tailSeconds = 1.0;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(), rendered, options);
    REQUIRE (report.ok());

    const auto musicEnds = 16.0 * 60.0 / 124.0;

    const auto duringMusic = peakBetween (rendered, 0.0, musicEnds, options.sampleRate);
    const auto lateTail = peakBetween (rendered, musicEnds + 0.5, report.seconds, options.sampleRate);

    REQUIRE (duringMusic > 0.05f);
    REQUIRE (lateTail < duringMusic * 0.05f);
}

TEST_CASE ("an explicit length renders exactly that long and keeps looping", "[engine][render]")
{
    RenderOptions options;
    options.seconds = 4.0;
    options.mode = Transport::Mode::pattern;
    options.patternId = 1;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(), rendered, options);

    REQUIRE (report.ok());
    REQUIRE (report.seconds == Approx (4.0));
    REQUIRE (rendered.getNumSamples() == 4 * 44100);

    // One 16-step pattern at 124 bpm is ~1.94 s, so four seconds must still be
    // sounding at the end because it looped.
    REQUIRE (peakBetween (rendered, 3.5, 4.0, options.sampleRate) > 0.01f);
}

TEST_CASE ("a project with nothing to play is refused rather than rendered silent", "[engine][render]")
{
    // A default project has channels and a pattern, but no notes and no clips.
    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDefault(), rendered);

    REQUIRE (! report.ok());
    REQUIRE (report.result.getErrorMessage().contains ("playlist"));
}

TEST_CASE ("muting a channel removes it from the mix", "[engine][render]")
{
    auto project = ProjectFactory::createDemo();

    juce::AudioBuffer<float> full;
    const auto before = OfflineRenderer::renderToBuffer (project, full);
    REQUIRE (before.ok());

    for (auto channel : project)
        if (channel.hasType (ids::CHANNEL))
            channel.setProperty (ids::muted, true, nullptr);

    juce::AudioBuffer<float> muted;
    const auto after = OfflineRenderer::renderToBuffer (project, muted);
    REQUIRE (after.ok());

    REQUIRE (peakOf (full) > 0.05f);
    // Exactly zero, not merely quiet: a muted channel must contribute nothing.
    REQUIRE (juce::exactlyEqual (peakOf (muted), 0.0f));
}

TEST_CASE ("soloing one mixer track silences the others", "[engine][render]")
{
    auto project = ProjectFactory::createDemo();

    auto mixer = project.getChildWithName (ids::MIXER);

    // Solo insert 1, which only the kick is routed to.
    for (auto track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            track.setProperty (ids::solo, (int) track[ids::id] == 1, nullptr);

    juce::AudioBuffer<float> soloed;
    const auto report = OfflineRenderer::renderToBuffer (project, soloed);
    REQUIRE (report.ok());

    // Still audible - the kick is playing.
    REQUIRE (peakOf (soloed) > 0.01f);

    juce::AudioBuffer<float> everything;
    OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(), everything);

    // Quieter than the full mix, because three channels are gone.
    REQUIRE (report.rms < 0.9f * 0.5f * (everything.getRMSLevel (0, 0, everything.getNumSamples())
                                         + everything.getRMSLevel (1, 0, everything.getNumSamples())));
}

TEST_CASE ("a project saved and reloaded renders identically", "[engine][render][io]")
{
    // The end-to-end claim: new -> edit -> save -> open loses nothing that
    // affects what you hear.
    const auto original = ProjectFactory::createDemo();

    juce::TemporaryFile temp (".dew");
    REQUIRE (ProjectSerializer::writeToFile (original, temp.getFile()).wasOk());

    const auto reloaded = ProjectSerializer::readFromFile (temp.getFile());
    REQUIRE (reloaded.ok());

    juce::AudioBuffer<float> before, after;
    REQUIRE (OfflineRenderer::renderToBuffer (original, before).ok());
    REQUIRE (OfflineRenderer::renderToBuffer (reloaded.tree, after).ok());

    REQUIRE (before.getNumSamples() == after.getNumSamples());
    REQUIRE (before.getNumChannels() == after.getNumChannels());

    for (int channel = 0; channel < before.getNumChannels(); ++channel)
    {
        const auto* a = before.getReadPointer (channel);
        const auto* b = after.getReadPointer (channel);

        for (int i = 0; i < before.getNumSamples(); ++i)
        {
            if (! juce::exactlyEqual (a[i], b[i]))
            {
                INFO ("channel " << channel << " differs at sample " << i);
                REQUIRE (false);
            }
        }
    }
}

TEST_CASE ("the committed example project is loadable and audible", "[engine][render][demo]")
{
    // examples/demo.dew is committed and used by CI. If it drifts from what the
    // factory produces, or stops rendering, this catches it here rather than
    // in a CI step that is harder to read.
    const auto file = juce::File (DEW_EXAMPLES_DIR).getChildFile ("demo.dew");

    INFO ("looking for " << file.getFullPathName());
    REQUIRE (file.existsAsFile());

    const auto loaded = ProjectSerializer::readFromFile (file);
    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());

    REQUIRE (loaded.tree.isEquivalentTo (ProjectFactory::createDemo()));

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (loaded.tree, rendered);
    REQUIRE (report.ok());
    REQUIRE (report.peak > 0.05f);
}
