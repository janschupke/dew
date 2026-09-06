#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

juce::ValueTree firstPlaylistTrack (juce::ValueTree project)
{
    for (auto track : project.getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK))
            return track;

    return {};
}

} // namespace

TEST_CASE ("the three clip kinds are exhaustive and mutually exclusive", "[schema][audio]")
{
    auto project = ProjectFactory::createDefault();
    auto track = firstPlaylistTrack (project);
    REQUIRE (track.isValid());

    const auto midi = ProjectEdits::addClip (track, 1, 0 * 16, 1 * 16, nullptr);
    const auto automation = ProjectEdits::addAutomationClip (track, 1, 1 * 16, 1 * 16, nullptr);
    const auto audio = ProjectEdits::addAudioClip (track, 1, 2 * 16, 1 * 16, nullptr);

    REQUIRE (ProjectEdits::isMidiClip (midi));
    REQUIRE (! ProjectEdits::isAutomationClip (midi));
    REQUIRE (! ProjectEdits::isAudioClip (midi));

    REQUIRE (ProjectEdits::isAutomationClip (automation));
    REQUIRE (! ProjectEdits::isMidiClip (automation));
    REQUIRE (! ProjectEdits::isAudioClip (automation));

    REQUIRE (ProjectEdits::isAudioClip (audio));
    REQUIRE (! ProjectEdits::isMidiClip (audio));
    REQUIRE (! ProjectEdits::isAutomationClip (audio));
}

TEST_CASE ("a clip with no kind at all reads as MIDI", "[schema][audio]")
{
    // A version 3 file has clips with no `kind`. Those are notes, and defining
    // "MIDI" by exclusion rather than as == "pattern" is what keeps them so.
    juce::ValueTree clip (ids::CLIP);

    REQUIRE (ProjectEdits::isMidiClip (clip));
    REQUIRE (! ProjectEdits::isAudioClip (clip));
    REQUIRE (! ProjectEdits::isAutomationClip (clip));
}

TEST_CASE ("an audio channel and its clip round-trip through JSON", "[schema][audio]")
{
    auto project = ProjectFactory::createDefault();

    auto channel = ProjectEdits::addAudioChannel (project, "Take", nullptr);
    REQUIRE (ProjectEdits::playsClips (channel));

    ProjectEdits::setSampleSource (channel, "Song Assets/Take 001.wav", 48000, 96000, nullptr);

    auto sample = channel.getChildWithName (ids::SAMPLE);
    REQUIRE (sample.isValid());

    sample.setProperty (ids::startSample, 240, nullptr);
    sample.setProperty (ids::endSample, 90000, nullptr);
    sample.setProperty (ids::fadeInMs, 12.5, nullptr);
    sample.setProperty (ids::transpose, -3.0, nullptr);
    sample.setProperty (ids::reverse, true, nullptr);
    sample.setProperty (ids::loop, true, nullptr);

    auto track = firstPlaylistTrack (project);
    ProjectEdits::addAudioClip (track, (int) channel[ids::id], 4 * 16, 2 * 16, nullptr);

    const auto json = ProjectSerializer::toJsonString (project);
    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());
    REQUIRE (loaded.tree.isValid());

    auto reloaded = ProjectEdits::findChannel (loaded.tree, (int) channel[ids::id]);
    REQUIRE (ProjectEdits::playsClips (reloaded));

    auto reloadedSample = reloaded.getChildWithName (ids::SAMPLE);
    REQUIRE (reloadedSample[ids::file].toString() == "Song Assets/Take 001.wav");
    REQUIRE ((int) reloadedSample[ids::sourceSampleRate] == 48000);
    REQUIRE ((int) reloadedSample[ids::lengthSamples] == 96000);
    REQUIRE ((int) reloadedSample[ids::startSample] == 240);
    REQUIRE ((int) reloadedSample[ids::endSample] == 90000);
    REQUIRE ((bool) reloadedSample[ids::reverse]);
    REQUIRE ((bool) reloadedSample[ids::loop]);

    auto reloadedTrack = firstPlaylistTrack (loaded.tree);
    auto reloadedClip = ProjectEdits::findClipAtStep (reloadedTrack, 4 * 16);

    REQUIRE (ProjectEdits::isAudioClip (reloadedClip));
    REQUIRE ((int) reloadedClip[ids::channelId] == (int) channel[ids::id]);
}

TEST_CASE ("a synth channel still carries an inert sample node", "[schema][audio]")
{
    // Declared for every channel, like the three oscillator slots most of which
    // are off: the editor has to be able to point at a slot before you have
    // committed to using it, and the canonical tree stays one shape.
    const auto project = ProjectFactory::createDefault();

    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        const auto sample = channel.getChildWithName (ids::SAMPLE);

        REQUIRE (sample.isValid());
        REQUIRE (sample[ids::file].toString().isEmpty());
        REQUIRE (! ProjectEdits::playsClips (channel));
    }
}

TEST_CASE ("a file written before audio existed still loads", "[schema][audio]")
{
    // The v7 additions are all properties and one child with declared defaults,
    // so an older file is simply one that predates them. This is the assertion
    // that keeps the version bump from needing a migration.
    const auto json = ProjectSerializer::toJsonString (dew::testing::fixtureProject());

    auto parsed = juce::JSON::parse (json);
    REQUIRE (parsed.isObject());

    auto* root = parsed.getDynamicObject();
    REQUIRE (root != nullptr);

    // Claim to be the version before audio, and take the v7 keys back out -
    // through the parsed object rather than by deleting text, so what is left
    // is a genuinely well-formed older file.
    root->setProperty ("formatVersion", 6);

    if (auto* channels = root->getProperty ("channels").getArray())
    {
        for (auto& channel : *channels)
        {
            if (auto* object = channel.getDynamicObject())
            {
                object->removeProperty ("sample");
                object->removeProperty ("source");
            }
        }
    }

    if (auto* playlist = root->getProperty ("playlist").getDynamicObject())
    {
        if (auto* tracks = playlist->getProperty ("tracks").getArray())
        {
            for (auto& track : *tracks)
            {
                if (auto* object = track.getDynamicObject())
                {
                    if (auto* clips = object->getProperty ("clips").getArray())
                    {
                        for (auto& clip : *clips)
                        {
                            if (auto* clipObject = clip.getDynamicObject())
                                clipObject->removeProperty ("channelId");
                        }
                    }
                }
            }
        }
    }

    const auto loaded = ProjectSerializer::fromJsonString (juce::JSON::toString (parsed));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.tree.isValid());

    // Every channel comes back with an inert SAMPLE node materialised from the
    // spec, and none of them has silently become an audio channel.
    auto sawChannel = false;

    for (const auto& channel : loaded.tree)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        sawChannel = true;
        REQUIRE (channel.getChildWithName (ids::SAMPLE).isValid());
        REQUIRE (! ProjectEdits::playsClips (channel));
    }

    REQUIRE (sawChannel);
}

TEST_CASE ("adding an audio channel is one undo step", "[edits][audio]")
{
    ProjectDocument document;
    auto& undo = document.getUndoManager();

    const auto before = document.getState().getNumChildren();

    undo.beginNewTransaction ("Add audio channel");
    const auto channel = ProjectEdits::addAudioChannel (document.getState(), "Take", &undo);

    REQUIRE (ProjectEdits::playsClips (channel));

    // The kind is set inside the same transaction, so undo cannot leave a synth
    // channel behind where an audio one was asked for.
    REQUIRE (undo.undo());
    REQUIRE (document.getState().getNumChildren() == before);
}
