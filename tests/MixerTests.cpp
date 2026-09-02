#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/MixerBus.h"

using namespace dew;
using Catch::Approx;

namespace
{

EngineSnapshot withTracks (std::initializer_list<std::pair<bool, bool>> muteSolo)
{
    EngineSnapshot s;

    for (auto [mute, solo] : muteSolo)
    {
        MixerTrackSnapshot t;
        t.id = (int) s.mixerTracks.size() + 1;
        t.mute = mute;
        t.solo = solo;
        s.anySolo = s.anySolo || solo;
        s.mixerTracks.push_back (t);
    }

    return s;
}

} // namespace

TEST_CASE ("mute silences a track", "[mixer]")
{
    const auto snapshot = withTracks ({ { false, false }, { true, false } });

    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[0]));
    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[1]));
}

TEST_CASE ("solo anywhere silences every track that is not soloed", "[mixer]")
{
    const auto snapshot = withTracks ({ { false, false }, { false, true }, { false, false } });

    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[0]));
    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[1]));
    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[2]));
}

TEST_CASE ("mute beats solo on the same track", "[mixer]")
{
    // A track both muted and soloed stays silent: mute is the explicit "off".
    const auto snapshot = withTracks ({ { true, true }, { false, true } });

    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[0]));
    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[1]));
}

TEST_CASE ("panning is constant power", "[mixer]")
{
    float left = 0.0f, right = 0.0f;

    MixerBus::panGains (0.0f, left, right);
    REQUIRE (left == Approx (right));
    REQUIRE (left * left + right * right == Approx (1.0f));

    MixerBus::panGains (-1.0f, left, right);
    REQUIRE (left == Approx (1.0f));
    REQUIRE (right == Approx (0.0f).margin (1.0e-6));

    MixerBus::panGains (1.0f, left, right);
    REQUIRE (left == Approx (0.0f).margin (1.0e-6));
    REQUIRE (right == Approx (1.0f));

    // Power stays constant across the sweep - no dip in the middle.
    for (float pan = -1.0f; pan <= 1.0f; pan += 0.1f)
    {
        MixerBus::panGains (pan, left, right);
        REQUIRE (left * left + right * right == Approx (1.0f).epsilon (0.0001));
    }
}

TEST_CASE ("out-of-range pan is clamped rather than wrapped", "[mixer]")
{
    float left = 0.0f, right = 0.0f;
    float clampedLeft = 0.0f, clampedRight = 0.0f;

    MixerBus::panGains (-5.0f, left, right);
    MixerBus::panGains (-1.0f, clampedLeft, clampedRight);

    REQUIRE (left == Approx (clampedLeft));
    REQUIRE (right == Approx (clampedRight));
}

// --- the mixer's UI ----------------------------------------------------------

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "ui/EditorState.h"
#include "ui/MixerComponent.h"

TEST_CASE ("the master carries an effect chain like any other bus", "[mixer][effects]")
{
    // Every insert could hold a chain and the master could not, which read as
    // an omission rather than as a rule.
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto master = project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
    REQUIRE (master.isValid());

    auto effect = ProjectEdits::addEffect (project, master, "filter", &undo);
    REQUIRE (effect.isValid());
    effect.setProperty (ids::cutoff, 120.0, &undo);

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (warnings.isEmpty());
    REQUIRE (snapshot.masterEffects.numSlots == 1);
    REQUIRE (snapshot.anyEffects);

    RenderOptions options;
    options.seconds = 2.0;
    options.mode = Transport::Mode::pattern;

    juce::AudioBuffer<float> filtered;
    const auto report = OfflineRenderer::renderToBuffer (project, filtered, options);
    REQUIRE (report.ok());

    // A steep lowpass on the master takes the whole mix down, because it is on
    // the summed signal rather than on one track.
    auto plainProject = ProjectFactory::createDemo();
    juce::AudioBuffer<float> plain;
    const auto plainReport = OfflineRenderer::renderToBuffer (plainProject, plain, options);

    INFO ("plain rms " << plainReport.rms << " master-filtered rms " << report.rms);
    REQUIRE (report.rms < plainReport.rms * 0.6f);
    REQUIRE (report.rms > 0.0f);
}

TEST_CASE ("a project without a master chain still loads", "[mixer][schema]")
{
    // formatVersion 4 files have no effects array on the master at all.
    const juce::String version4 = R"({
        "format": "dew-project",
        "formatVersion": 4,
        "name": "older",
        "tempoBpm": 120.0,
        "stepsPerBeat": 4,
        "barsInSong": 4,
        "channels": [],
        "patterns": [],
        "automations": [],
        "playlist": { "tracks": [] },
        "mixer": { "master": { "gain": 0.8 }, "tracks": [] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (version4);

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());

    const auto master = loaded.tree.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
    REQUIRE (master.isValid());
    REQUIRE (juce::exactlyEqual ((double) master[ids::gain], 0.8));
    REQUIRE (ProjectEdits::countEffects (master) == 0);
}

TEST_CASE ("each strip lists the channels routed into it", "[mixer][ui]")
{
    // Without this the mixer is a row of anonymous faders with nothing saying
    // what any of them carries.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDemo(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1000, 700);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    juce::Image image (juce::Image::ARGB, mixer.getWidth(), mixer.getHeight(), true);
    juce::Graphics g (image);
    mixer.paintEntireComponent (g, true);

    // Each demo channel's colour appears somewhere in the mixer, as its routing
    // dot on the insert it feeds.
    for (const auto& channel : document.getState())
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        const auto target = juce::Colour::fromString (
            "ff" + channel[ids::colour].toString().getLastCharacters (6));

        int matching = 0;

        for (int y = 0; y < image.getHeight(); y += 1)
            for (int x = 0; x < image.getWidth(); x += 1)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (std::abs ((int) pixel.getRed()   - (int) target.getRed())   < 20
                 && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) < 20
                 && std::abs ((int) pixel.getBlue()  - (int) target.getBlue())  < 20)
                    ++matching;
            }

        INFO ("channel " << channel[ids::name].toString() << " dot pixels: " << matching);
        REQUIRE (matching > 0);
    }
}

TEST_CASE ("strips scroll rather than vanishing when there are many", "[mixer][ui]")
{
    // removeFromLeft on a fixed rectangle clamps at the right edge, so past
    // about twelve inserts every further strip - including the master, added
    // last - was silently given no width.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    juce::UndoManager& undo = document.getUndoManager();
    auto mixerTree = document.getState().getChildWithName (ids::MIXER);

    for (int i = 5; i <= 24; ++i)
    {
        juce::ValueTree track (ids::MIXER_TRACK);
        track.setProperty (ids::id, i, nullptr);
        track.setProperty (ids::name, "Insert " + juce::String (i), nullptr);
        track.setProperty (ids::gain, 0.8, nullptr);
        mixerTree.appendChild (track, &undo);
    }

    MixerComponent mixer { document, editorState };
    mixer.setSize (900, 600);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    juce::Array<juce::Component*> faders;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* slider = dynamic_cast<juce::Slider*> (child);
                slider != nullptr && slider->getSliderStyle() == juce::Slider::LinearVertical)
                faders.add (slider->getParentComponent());

            walk (*child);
        }
    };

    walk (mixer);

    // Twenty-four inserts plus the master, and every one of them has real width.
    REQUIRE (faders.size() == 25);

    for (auto* strip : faders)
    {
        INFO ("strip width " << strip->getWidth());
        REQUIRE (strip->getWidth() > 20);
    }
}
