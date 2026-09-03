#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EngineSnapshot.h"
#include "engine/TempoMap.h"
#include "engine/Transport.h"
#include "io/MidiExporter.h"
#include "model/DemoLibrary.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Matchers::WithinAbs;

namespace
{

/** A ramp from the bottom of the tempo range to the top, so the curve is
    unmistakably not constant. */
void setTempoCurve (juce::ValueTree automation, juce::UndoManager* undo)
{
    const auto points = ProjectEdits::sortedAutomationPoints (automation);

    points.getFirst().setProperty (ids::value, 0.0, undo);
    points.getLast().setProperty (ids::value, 1.0, undo);
}

} // namespace

TEST_CASE ("with no tempo automation the map is the arithmetic it replaces", "[tempo]")
{
    // The whole point of isConstant(), and the reason it is a correctness
    // requirement rather than an optimisation: dew's render tests compare
    // samples with juce::exactlyEqual, and rearranging the multiply changes the
    // last bit at some tempos. A project with no tempo curve has to take the
    // arithmetic it has always taken.
    for (const auto bpm : { 20.0, 96.0, 120.0, 124.0, 128.5, 174.0, 333.3, 999.0 })
    {
        for (const auto stepsPerBeat : { 1, 3, 4, 6, 7, 16 })
        {
            for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
            {
                Transport transport;
                transport.prepare (sampleRate);
                transport.setTempo (bpm, stepsPerBeat);

                const auto map = TempoMap::constant (bpm, stepsPerBeat);
                transport.setTempoMap (&map);

                const auto expected = Transport::samplesPerStepFor (bpm, stepsPerBeat, sampleRate);

                for (const auto steps : { 0.0, 1.0, 16.0, 63.0, 1024.0, 99999.0 })
                {
                    INFO (bpm << "bpm, " << stepsPerBeat << " steps/beat, " << sampleRate << "Hz, "
                              << steps << " steps");

                    REQUIRE (juce::exactlyEqual (transport.samplesForSteps (steps),
                                                 expected * steps));
                }
            }
        }
    }
}

TEST_CASE ("a map is exactly invertible at every whole step", "[tempo]")
{
    auto project = ProjectFactory::createDefault();
    juce::StringArray warnings;

    const auto snapshot = buildSnapshot (project, &warnings);
    REQUIRE (snapshot.tempoMap != nullptr);

    const auto& map = *snapshot.tempoMap;

    for (int step = 0; step <= 256; ++step)
    {
        INFO ("step " << step);
        REQUIRE_THAT (map.stepsForSeconds (map.secondsForSteps ((double) step)),
                      WithinAbs ((double) step, 1e-9));
    }

    // And between them, which is where a playhead actually sits.
    for (const auto step : { 0.5, 7.25, 63.125, 200.75 })
    {
        INFO ("step " << step);
        REQUIRE_THAT (map.stepsForSeconds (map.secondsForSteps (step)), WithinAbs (step, 1e-9));
    }
}

TEST_CASE ("a map is strictly monotone", "[tempo]")
{
    // Time going backwards would put the playhead behind itself and make
    // wrappedIntoLoop fold the wrong way.
    auto project = ProjectFactory::createDefault();
    const auto snapshot = buildSnapshot (project, nullptr);

    const auto& map = *snapshot.tempoMap;

    auto previous = map.secondsForSteps (-10.0);

    for (int step = -9; step <= 512; ++step)
    {
        const auto now = map.secondsForSteps ((double) step);

        INFO ("step " << step << ": " << previous << " -> " << now);
        REQUIRE (now > previous);

        previous = now;
    }
}

TEST_CASE ("the guard renders' fixture builds a constant map", "[tempo]")
{
    // This is the assertion that used to be made of the demos, and it belongs
    // here now: the renders that compare samples with exactlyEqual run on the
    // fixture, so the fixture is what has to take the fast path for them to be
    // comparing what they have always compared.
    const auto snapshot = buildSnapshot (dew::testing::fixtureProject(), nullptr);

    REQUIRE (snapshot.tempoMap != nullptr);
    REQUIRE (snapshot.tempoMap->isConstant());
}

TEST_CASE ("a demo's map is constant exactly when it draws no tempo", "[tempo]")
{
    // Stronger than "every demo is constant", which was only true while nothing
    // shipped used the scope. Both directions are checked, so a demo that gains
    // a tempo curve is noticed here, and so is one that claims to have one and
    // whose curve the engine did not pick up.
    const auto& entries = DemoLibrary::entries();
    REQUIRE (! entries.empty());

    int drawn = 0;

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        INFO ("demo " << entries[(size_t) i].fileName);

        juce::StringArray warnings;
        const auto tree = DemoLibrary::load (i, warnings);
        REQUIRE (tree.isValid());

        bool drawsTempo = false;

        for (const auto& automation : tree)
            if (automation.hasType (ids::AUTOMATION)
                && automation[ids::scope].toString() == "project"
                && automation[ids::param].toString() == ids::tempoBpm.toString())
                drawsTempo = true;

        const auto snapshot = buildSnapshot (tree, nullptr);
        REQUIRE (snapshot.tempoMap != nullptr);

        INFO ((drawsTempo ? "draws a tempo" : "draws no tempo"));
        REQUIRE (snapshot.tempoMap->isConstant() == ! drawsTempo);

        drawn += drawsTempo ? 1 : 0;
    }

    // Control case: a test over a library where nobody draws one proves nothing
    // about the branch it exists to check.
    REQUIRE (drawn >= 1);
}

TEST_CASE ("a snapshot always has a map, even an empty one", "[tempo]")
{
    // Everything that turns a step into time reads it, so a null check on that
    // path would be a branch in the render loop guarding a state that should not
    // exist.
    const auto empty = buildSnapshot ({}, nullptr);

    REQUIRE (empty.tempoMap != nullptr);
    REQUIRE (empty.tempoMap->isConstant());
}

TEST_CASE ("tempo is a target the picker offers", "[tempo][automation]")
{
    auto project = ProjectFactory::createDefault();

    juce::StringArray names;

    for (const auto& target : availableAutomationTargets (project))
        names.add (target.displayName);

    INFO ("first few: " << names.joinIntoString (", ").substring (0, 200));
    REQUIRE (names.contains ("Song > Tempo"));

    // FIRST, because the picker groups by the first word of a display name and
    // a global parameter buried after thirty channels is one nobody finds.
    REQUIRE (names[0] == "Song > Tempo");
}

TEST_CASE ("a tempo curve makes the arrangement longer", "[tempo][render]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    AutomationTarget tempo;

    for (const auto& target : availableAutomationTargets (project))
        if (target.displayName == "Song > Tempo")
            tempo = target;

    REQUIRE (tempo.spec != nullptr);

    const auto before = buildSnapshot (project, nullptr);
    REQUIRE (before.tempoMap->isConstant());

    auto automation = ProjectEdits::addAutomation (project, tempo, &undo);
    REQUIRE (automation.isValid());

    // Held at the bottom of the range for the whole clip, which is far slower
    // than the project's own 128.
    for (auto point : ProjectEdits::sortedAutomationPoints (automation))
        point.setProperty (ids::value, 0.0, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0, 4, &undo);

    const auto after = buildSnapshot (project, nullptr);

    REQUIRE_FALSE (after.tempoMap->isConstant());

    const auto steps = (double) after.songLengthSteps();

    INFO ("without " << before.tempoMap->secondsForSteps (steps)
          << "s, with " << after.tempoMap->secondsForSteps (steps) << "s");

    // Slower over the bars the clip covers, so the arrangement takes longer.
    REQUIRE (after.tempoMap->secondsForSteps (steps) > before.tempoMap->secondsForSteps (steps));

    // And still exactly invertible, which a hand-built table easily is not.
    for (int step = 0; step <= (int) steps; ++step)
        REQUIRE_THAT (after.tempoMap->stepsForSeconds (after.tempoMap->secondsForSteps ((double) step)),
                      WithinAbs ((double) step, 1e-9));
}

TEST_CASE ("a tempo clip only affects the bars it covers", "[tempo]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    AutomationTarget tempo;

    for (const auto& target : availableAutomationTargets (project))
        if (target.displayName == "Song > Tempo")
            tempo = target;

    REQUIRE (tempo.spec != nullptr);

    auto automation = ProjectEdits::addAutomation (project, tempo, &undo);

    for (auto point : ProjectEdits::sortedAutomationPoints (automation))
        point.setProperty (ids::value, 0.0, &undo);

    // Bars four to eight, so the first four are untouched.
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 4, 4, &undo);

    const auto snapshot = buildSnapshot (project, nullptr);
    const auto& map = *snapshot.tempoMap;

    const auto stepsPerBar = (double) snapshot.stepsPerBar();
    const auto plain = TempoMap::constant (snapshot.tempoBpm, snapshot.stepsPerBeat);

    // Identical up to bar four...
    for (const auto bar : { 0.0, 1.0, 2.0, 3.0, 4.0 })
    {
        INFO ("bar " << bar);
        REQUIRE_THAT (map.secondsForSteps (bar * stepsPerBar),
                      WithinAbs (plain.secondsForSteps (bar * stepsPerBar), 1e-9));
    }

    // ...and slower after it, which is what says the clip's bounds are honoured
    // rather than the curve being applied to the whole song.
    REQUIRE (map.secondsForSteps (8.0 * stepsPerBar) > plain.secondsForSteps (8.0 * stepsPerBar));
}

TEST_CASE ("an exported ramp moves no note", "[tempo][midi]")
{
    // The claim a tempo meta event makes: a tick is MUSICAL time, and the tempo
    // map is precisely the tick-to-seconds function. So a ramp changes how long
    // the file takes to play and not where a single note sits in it.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    const auto ticksOf = [] (const juce::ValueTree& tree)
    {
        juce::StringArray warnings;
        juce::int64 numNotes = 0;

        const auto file = MidiExporter::build (tree, {}, warnings, numNotes);

        juce::Array<double> ticks;

        for (int t = 0; t < file.getNumTracks(); ++t)
            for (const auto* event : *file.getTrack (t))
                if (event->message.isNoteOn())
                    ticks.add (event->message.getTimeStamp());

        ticks.sort();
        return ticks;
    };

    const auto before = ticksOf (project);
    REQUIRE (before.size() > 8);

    AutomationTarget tempo;

    for (const auto& target : availableAutomationTargets (project))
        if (target.displayName == "Song > Tempo")
            tempo = target;

    auto automation = ProjectEdits::addAutomation (project, tempo, &undo);
    setTempoCurve (automation, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0, 4, &undo);

    const auto after = ticksOf (project);

    REQUIRE (after.size() == before.size());

    for (int i = 0; i < before.size(); ++i)
    {
        INFO ("note " << i);
        REQUIRE_THAT (after[i], WithinAbs (before[i], 1e-9));
    }
}
