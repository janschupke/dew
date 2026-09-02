#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectFactory.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** A scratch directory that takes its contents with it. */
struct ScratchFolder
{
    ScratchFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("dew-stems-" + juce::Uuid().toString()))
    {
    }

    ~ScratchFolder() { folder.deleteRecursively(); }

    juce::File folder;
};

RenderOptions shortRender()
{
    RenderOptions options;
    options.seconds = 0.4;
    return options;
}

int numMixerTracks (const juce::ValueTree& project)
{
    int count = 0;

    if (const auto mixer = project.getChildWithName (ids::MIXER); mixer.isValid())
        for (const auto& track : mixer)
            if (track.hasType (ids::MIXER_TRACK))
                ++count;

    return count;
}

} // namespace

TEST_CASE ("stems produce one file per track that has something in it",
           "[engine][render][stems]")
{
    ScratchFolder scratch;

    const auto project = ProjectFactory::createDemo();
    const auto report = OfflineRenderer::renderStems (project, scratch.folder, shortRender());

    INFO (report.result.getErrorMessage());
    REQUIRE (report.ok());
    REQUIRE (report.files.size() > 0);
    REQUIRE (report.files.size() <= numMixerTracks (project));

    for (const auto& file : report.files)
    {
        INFO (file.getFullPathName());
        REQUIRE (file.existsAsFile());
        REQUIRE (file.getSize() > 0);
        REQUIRE (file.getFileExtension() == ".wav");
    }
}

TEST_CASE ("every stem is audible on its own", "[engine][render][stems]")
{
    ScratchFolder scratch;

    const auto report = OfflineRenderer::renderStems (ProjectFactory::createDemo(),
                                                       scratch.folder, shortRender());

    REQUIRE (report.ok());

    juce::WavAudioFormat format;

    for (const auto& file : report.files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (
            format.createReaderFor (file.createInputStream().release(), true));

        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);

        // A written stem that is silent means the skip rule did not work.
        INFO (file.getFileName());
        REQUIRE (audio.getMagnitude (0, audio.getNumSamples()) > 0.0f);
    }
}

TEST_CASE ("stem files are named for their tracks, in mixer order",
           "[engine][render][stems]")
{
    ScratchFolder scratch;

    const auto project = ProjectFactory::createDemo();
    const auto report = OfflineRenderer::renderStems (project, scratch.folder, shortRender());

    REQUIRE (report.ok());

    juce::StringArray names;

    if (const auto mixer = project.getChildWithName (ids::MIXER); mixer.isValid())
        for (const auto& track : mixer)
            if (track.hasType (ids::MIXER_TRACK))
                names.add (track[ids::name].toString());

    for (const auto& file : report.files)
    {
        const auto stem = file.getFileNameWithoutExtension();

        // The index prefix keeps the mixer's order and separates two inserts
        // that were given the same name.
        REQUIRE (stem.substring (0, 2).containsOnly ("0123456789"));

        bool matchesSomeTrack = false;

        for (const auto& name : names)
            matchesSomeTrack = matchesSomeTrack
                               || stem.contains (juce::File::createLegalFileName (name));

        INFO (stem);
        REQUIRE (matchesSomeTrack);
    }
}

TEST_CASE ("a track nothing is routed to is skipped rather than written empty",
           "[engine][render][stems]")
{
    const auto project = ProjectFactory::createDemo();

    ScratchFolder kept, skipped;

    auto keepEverything = shortRender();
    keepEverything.skipSilentStems = false;

    const auto all = OfflineRenderer::renderStems (project, kept.folder, keepEverything);
    const auto audible = OfflineRenderer::renderStems (project, skipped.folder, shortRender());

    REQUIRE (all.ok());
    REQUIRE (audible.ok());

    REQUIRE (all.files.size() == numMixerTracks (project));
    REQUIRE (audible.files.size() <= all.files.size());
}

TEST_CASE ("soloing one track still yields a stem for every track",
           "[engine][render][stems]")
{
    auto project = ProjectFactory::createDemo();

    auto mixer = project.getChildWithName (ids::MIXER);
    REQUIRE (mixer.isValid());

    juce::ValueTree first;

    for (auto track : mixer)
        if (track.hasType (ids::MIXER_TRACK) && ! first.isValid())
            first = track;

    REQUIRE (first.isValid());

    ScratchFolder before, after;

    const auto plain = OfflineRenderer::renderStems (project, before.folder, shortRender());

    first.setProperty (ids::solo, true, nullptr);

    const auto soloed = OfflineRenderer::renderStems (project, after.folder, shortRender());

    REQUIRE (plain.ok());
    REQUIRE (soloed.ok());

    // Stems exist to give you every track. Honouring a solo here would hand back
    // one file and silence.
    REQUIRE (soloed.files.size() == plain.files.size());
}

TEST_CASE ("a muted track is still exported as its own stem", "[engine][render][stems]")
{
    auto project = ProjectFactory::createDemo();

    auto mixer = project.getChildWithName (ids::MIXER);
    juce::ValueTree first;

    for (auto track : mixer)
        if (track.hasType (ids::MIXER_TRACK) && ! first.isValid())
            first = track;

    REQUIRE (first.isValid());
    first.setProperty (ids::mute, true, nullptr);

    ScratchFolder scratch;

    auto options = shortRender();
    options.skipSilentStems = false;

    const auto report = OfflineRenderer::renderStems (project, scratch.folder, options);

    REQUIRE (report.ok());
    REQUIRE (report.files.size() == numMixerTracks (project));
}

TEST_CASE ("stems warn when the master chain will stop them summing",
           "[engine][render][stems]")
{
    auto project = ProjectFactory::createDemo();

    auto master = project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
    REQUIRE (master.isValid());

    ScratchFolder clean, processed;

    const auto plain = OfflineRenderer::renderStems (project, clean.folder, shortRender());
    REQUIRE (plain.ok());

    const auto mentionsSumming = [] (const RenderReport& report)
    {
        for (const auto& warning : report.warnings)
            if (warning.contains ("sum"))
                return true;

        return false;
    };

    REQUIRE_FALSE (mentionsSumming (plain));

    // A drive on the master is non-linear, so the stems genuinely stop adding up.
    juce::ValueTree effect (ids::EFFECT);
    effect.setProperty (ids::id, 900, nullptr);
    effect.setProperty (ids::type, "drive", nullptr);
    effect.setProperty (ids::enabled, true, nullptr);
    master.appendChild (effect, nullptr);

    const auto driven = OfflineRenderer::renderStems (project, processed.folder, shortRender());

    REQUIRE (driven.ok());
    REQUIRE (mentionsSumming (driven));
}

TEST_CASE ("stems can be cancelled part way", "[engine][render][stems]")
{
    ScratchFolder scratch;

    RenderProgress progress;
    progress.cancelled.store (true);

    const auto report = OfflineRenderer::renderStems (ProjectFactory::createDemo(),
                                                       scratch.folder, shortRender(), &progress);

    REQUIRE (report.cancelled);
    REQUIRE (report.ok());
}

TEST_CASE ("stems report progress across the whole set, not per file",
           "[engine][render][stems]")
{
    ScratchFolder scratch;

    RenderProgress progress;

    const auto report = OfflineRenderer::renderStems (ProjectFactory::createDemo(),
                                                       scratch.folder, shortRender(), &progress);

    REQUIRE (report.ok());
    REQUIRE (progress.fraction.load() == Approx (1.0));
}

TEST_CASE ("stems refuse MIDI, which is one file by nature", "[engine][render][stems]")
{
    ScratchFolder scratch;

    auto options = shortRender();
    options.format = RenderFormat::midi;

    const auto report = OfflineRenderer::renderStems (ProjectFactory::createDemo(),
                                                       scratch.folder, options);

    REQUIRE_FALSE (report.ok());
}
