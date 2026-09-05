// What a curve does to the audio when it is played.
//
// Split out of AutomationTests.cpp, along the Catch2 tags it already
// carried. The fixture is AutomationHarness.h.

#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EngineSnapshot.h"
#include "io/OfflineRenderer.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

#include "AutomationHarness.h"
#include "FixtureProject.h"

using namespace dew;
using namespace dew::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE ("an automation sweep is audible as a rising envelope", "[automation][render]")
{
    // The whole point of automation, measured through the real engine rather
    // than by inspecting the snapshot.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"),
                                                   &undo);
    REQUIRE (automation.isValid());

    // Silence to full over one bar, then held, at 16 steps a bar.
    setCurve (automation, { { 0.0, 0.0 }, { 16.0, 1.0 }, { 64.0, 1.0 } }, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0, 4, &undo);

    RenderOptions options;
    options.mode = Transport::Mode::song;
    options.seconds = 6.0;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (project, rendered, options);

    REQUIRE (report.ok());
    REQUIRE (report.warnings.isEmpty());

    const auto atStart = rmsOfWindow (rendered, 0.0, 0.4);
    const auto atMiddle = rmsOfWindow (rendered, 0.8, 1.2);
    const auto atEnd = rmsOfWindow (rendered, 1.6, 2.0);

    INFO ("rms " << atStart << " -> " << atMiddle << " -> " << atEnd);

    REQUIRE (atStart < atMiddle);
    REQUIRE (atMiddle < atEnd);
    REQUIRE (atEnd > 0.01f);
}

TEST_CASE ("an automation clip only acts where it is placed", "[automation][render]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"),
                                                   &undo);

    // A curve that is silent all the way through.
    setCurve (automation, { { 0.0, 0.0 }, { 64.0, 0.0 } }, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);

    // Placed on bars 3 and 4 only, so bars 1 and 2 keep the project's own gain.
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 2, 2, &undo);
    ProjectEdits::growSongToFitClips (project, &undo);

    RenderOptions options;
    options.mode = Transport::Mode::song;
    options.seconds = 6.0;

    juce::AudioBuffer<float> rendered;
    REQUIRE (OfflineRenderer::renderToBuffer (project, rendered, options).ok());

    // Two bars at 124bpm is about 3.87 seconds.
    const auto beforeClip = rmsOfWindow (rendered, 0.3, 1.5);
    const auto duringClip = rmsOfWindow (rendered, 4.2, 5.5);

    INFO ("before " << beforeClip << " during " << duringClip);
    REQUIRE (beforeClip > 0.01f);
    REQUIRE (duringClip < beforeClip * 0.05f);
}

TEST_CASE ("automating an effect parameter changes what the effect does", "[automation][render]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    // Only the first channel routes to Insert 1, so silence the others and the
    // whole render is what that one filter is doing.
    bool first = true;

    for (auto channel : project)
        if (channel.hasType (ids::CHANNEL))
        {
            if (! first)
                channel.setProperty (ids::muted, true, &undo);

            first = false;
        }

    auto mixerTrack = project.getChildWithName (ids::MIXER).getChildWithName (ids::MIXER_TRACK);
    auto filter = ProjectEdits::addEffect (project, mixerTrack, "filter", &undo);
    REQUIRE (filter.isValid());

    const auto trackName = mixerTrack[ids::name].toString();
    auto automation = ProjectEdits::addAutomation (
        project, targetNamed (project, trackName + " > Filter > Cutoff"), &undo);

    // Wide open, shut within the first bar, then held shut.
    setCurve (automation, { { 0.0, 1.0 }, { 16.0, 0.0 } }, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0, 4, &undo);

    RenderOptions options;
    options.mode = Transport::Mode::song;
    options.seconds = 6.0;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (project, rendered, options);

    REQUIRE (report.ok());
    REQUIRE (report.warnings.isEmpty());

    // One bar at 124bpm is about 1.94 seconds.
    const auto open = rmsOfWindow (rendered, 0.05, 0.5);
    const auto closed = rmsOfWindow (rendered, 2.5, 3.5);

    INFO ("open " << open << " closed " << closed);
    REQUIRE (open > 0.005f);
    REQUIRE (closed < open * 0.2f);
}

TEST_CASE ("a mute curve silences a channel and lets it back in", "[automation][render]")
{
    // Compared against the SAME project without the clip, not against its own
    // second half: a demo that happened to get louder would pass that on its own.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    juce::ValueTree first;

    for (auto channel : project)
        if (channel.hasType (ids::CHANNEL) && ! first.isValid())
            first = channel;

    REQUIRE (first.isValid());

    const auto render = [] (const juce::ValueTree& tree)
    {
        RenderOptions options;
        options.mode = Transport::Mode::song;
        options.seconds = 6.0;

        juce::AudioBuffer<float> buffer;
        const auto report = OfflineRenderer::renderToBuffer (tree, buffer, options);
        REQUIRE (report.ok());

        return buffer;
    };

    const auto without = render (project);

    // Muted for the first two bars, then heard. Stepped, so it is a jump at bar
    // three rather than a fade across the clip.
    auto automation = ProjectEdits::addAutomation (
        project, targetNamed (project, first[ids::name].toString() + " > Off"), &undo);
    setCurve (automation, { { 0.0, 1.0 }, { 32.0, 0.0 } }, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0, 4, &undo);

    const auto with = render (project);

    // Quieter than the unautomated render while the curve says muted...
    const auto mutedWindow = std::make_pair (0.05, 1.5);
    const auto heardWindow = std::make_pair (4.2, 5.5);

    const auto quietBefore = rmsOfWindow (without, mutedWindow.first, mutedWindow.second);
    const auto quietAfter = rmsOfWindow (with, mutedWindow.first, mutedWindow.second);

    INFO ("in the muted window: " << quietBefore << " -> " << quietAfter);
    REQUIRE (quietBefore > 0.0005f);
    REQUIRE (quietAfter < quietBefore * 0.9f);

    // ...and the same again once it says heard, which is what says the curve
    // let the channel back in rather than silencing it for good.
    const auto loudBefore = rmsOfWindow (without, heardWindow.first, heardWindow.second);
    const auto loudAfter = rmsOfWindow (with, heardWindow.first, heardWindow.second);

    INFO ("in the heard window: " << loudBefore << " -> " << loudAfter);
    REQUIRE (loudBefore > 0.0005f);
    REQUIRE_THAT ((double) loudAfter, WithinAbs ((double) loudBefore, (double) loudBefore * 0.05));
}

TEST_CASE ("a muted playlist track's automation does nothing", "[automation][render]")
{
    // A muted lane silences its notes; it has to silence what its automation
    // does too, or a muted track still moves the mix.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"),
                                                   &undo);
    setCurve (automation, { { 0.0, 0.0 }, { 64.0, 0.0 } }, &undo);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto automationTrack = playlist.getChild (1);
    ProjectEdits::addAutomationClip (automationTrack, (int) automation[ids::id], 0, 4, &undo);

    RenderOptions options;
    options.mode = Transport::Mode::song;
    options.seconds = 3.0;

    juce::AudioBuffer<float> silenced, restored;
    REQUIRE (OfflineRenderer::renderToBuffer (project, silenced, options).ok());
    REQUIRE (rmsOfWindow (silenced, 0.3, 2.0) < 0.001f);

    automationTrack.setProperty (ids::mute, true, &undo);
    REQUIRE (OfflineRenderer::renderToBuffer (project, restored, options).ok());
    REQUIRE (rmsOfWindow (restored, 0.3, 2.0) > 0.01f);
}
