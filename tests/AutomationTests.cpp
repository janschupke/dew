#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EngineSnapshot.h"
#include "io/OfflineRenderer.h"
#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

using namespace dew;
using Catch::Matchers::WithinAbs;

namespace
{

/** The target with this display name, so tests name what they are automating
    rather than indexing into a list whose order they would then depend on.
*/
AutomationTarget targetNamed (const juce::ValueTree& project, const juce::String& name)
{
    for (const auto& target : availableAutomationTargets (project))
        if (target.displayName == name)
            return target;

    FAIL ("no automation target named " << name);
    return {};
}

/** RMS of one window of a render, which is how "did this get louder" is
    actually measured.
*/
float rmsOfWindow (const juce::AudioBuffer<float>& buffer, double fromSeconds, double toSeconds,
                   double sampleRate = 44100.0)
{
    const auto start = juce::jlimit (0, buffer.getNumSamples() - 1, (int) (fromSeconds * sampleRate));
    const auto end = juce::jlimit (start, buffer.getNumSamples(), (int) (toSeconds * sampleRate));

    return end > start ? buffer.getRMSLevel (0, start, end - start) : 0.0f;
}

/** Replaces an automation's curve outright.

    addAutomation seeds two points so a new clip is a line rather than an empty
    box, and those seeds survive adding more - so a test that only adds points
    is testing a shape it did not draw. Learned the hard way.
*/
void setCurve (juce::ValueTree automation, std::initializer_list<std::pair<double, double>> curve,
               juce::UndoManager* undo)
{
    for (const auto& [step, value] : curve)
        ProjectEdits::addAutomationPoint (automation, step, value, undo);

    juce::Array<juce::ValueTree> unwanted;

    for (const auto& point : automation)
    {
        if (! point.hasType (ids::POINT))
            continue;

        bool wanted = false;

        for (const auto& [step, value] : curve)
        {
            juce::ignoreUnused (value);
            wanted = wanted || juce::approximatelyEqual ((double) point[ids::step], step);
        }

        if (! wanted)
            unwanted.add (point);
    }

    for (const auto& point : unwanted)
        ProjectEdits::removeAutomationPoint (automation, point, undo);
}

} // namespace

TEST_CASE ("a curve reads back the shape it was drawn as", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);
    REQUIRE (automation.isValid());

    // A fresh automation is a line, not an empty box.
    REQUIRE (ProjectEdits::automationValueAt (automation, 0.0) > 0.0);

    // A ramp from 0 to 1 over 32 steps.
    setCurve (automation, { { 0.0, 0.0 }, { 16.0, 0.5 }, { 32.0, 1.0 } }, &undo);

    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 0.0), WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.25, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 32.0), WithinAbs (1.0, 1e-9));

    // Held flat outside the points, rather than extrapolating off the end.
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, -50.0), WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 500.0), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("points stay in step order however they are dragged", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);

    auto a = ProjectEdits::addAutomationPoint (automation, 4.0, 0.2, &undo);
    ProjectEdits::addAutomationPoint (automation, 8.0, 0.8, &undo);

    // Drag the earlier point past the later one.
    ProjectEdits::moveAutomationPoint (automation, a, 20.0, 0.2, &undo);

    double previous = -1.0;

    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
        {
            REQUIRE ((double) point[ids::step] >= previous);
            previous = (double) point[ids::step];
        }

    // An unsorted curve would evaluate to a shape nobody drew.
    REQUIRE (ProjectEdits::automationValueAt (automation, 8.0) > 0.2);
}

TEST_CASE ("two points cannot share a step", "[automation]")
{
    // A step with two values has no defined value there.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);

    const auto countPoints = [&automation]
    {
        int n = 0;

        for (const auto& child : automation)
            if (child.hasType (ids::POINT))
                ++n;

        return n;
    };

    const auto before = countPoints();

    ProjectEdits::addAutomationPoint (automation, 12.0, 0.3, &undo);
    REQUIRE (countPoints() == before + 1);

    ProjectEdits::addAutomationPoint (automation, 12.0, 0.9, &undo);
    REQUIRE (countPoints() == before + 1);
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 12.0), WithinAbs (0.9, 1e-9));
}

TEST_CASE ("a curve keeps at least two points", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);

    juce::Array<juce::ValueTree> points;

    for (const auto& child : automation)
        if (child.hasType (ids::POINT))
            points.add (child);

    REQUIRE (points.size() == 2);

    ProjectEdits::removeAutomationPoint (automation, points.getFirst(), &undo);

    int remaining = 0;

    for (const auto& child : automation)
        if (child.hasType (ids::POINT))
            ++remaining;

    REQUIRE (remaining == 2);
}

TEST_CASE ("an automation sweep is audible as a rising envelope", "[automation][render]")
{
    // The whole point of automation, measured through the real engine rather
    // than by inspecting the snapshot.
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"), &undo);
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

    const auto atStart  = rmsOfWindow (rendered, 0.0, 0.4);
    const auto atMiddle = rmsOfWindow (rendered, 0.8, 1.2);
    const auto atEnd    = rmsOfWindow (rendered, 1.6, 2.0);

    INFO ("rms " << atStart << " -> " << atMiddle << " -> " << atEnd);

    REQUIRE (atStart < atMiddle);
    REQUIRE (atMiddle < atEnd);
    REQUIRE (atEnd > 0.01f);
}

TEST_CASE ("an automation clip only acts where it is placed", "[automation][render]")
{
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"), &undo);

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
    auto project = ProjectFactory::createDemo();
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
    auto automation = ProjectEdits::addAutomation (project,
                                                   targetNamed (project, trackName + " > Filter > Cutoff"),
                                                   &undo);

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

TEST_CASE ("frequency parameters sweep by ear, not by hertz", "[automation]")
{
    // A cutoff mapped linearly spends four fifths of a drawn curve above 3kHz.
    auto project = ProjectFactory::createDefault();

    const auto cutoffSpec = findParamSpec (AutomationScope::channelEffect, "filter", ids::cutoff);
    REQUIRE (cutoffSpec != nullptr);
    REQUIRE (cutoffSpec->logarithmic);

    // Halfway up the curve is the geometric middle of the range, which is
    // roughly where a listener would put "halfway".
    const auto middle = mapAutomationValue (*cutoffSpec, 0.5);
    INFO ("midpoint " << middle);
    REQUIRE (middle > 400.0);
    REQUIRE (middle < 1200.0);

    // The ends still land exactly on the range.
    REQUIRE_THAT (mapAutomationValue (*cutoffSpec, 0.0), WithinAbs (cutoffSpec->minimum, 1e-6));
    REQUIRE_THAT (mapAutomationValue (*cutoffSpec, 1.0), WithinAbs (cutoffSpec->maximum, 1e-3));

    // Gain-like parameters stay linear: half volume means half.
    const auto volumeSpec = findParamSpec (AutomationScope::channel, "", ids::volume);
    REQUIRE (volumeSpec != nullptr);
    REQUIRE (! volumeSpec->logarithmic);
    REQUIRE_THAT (mapAutomationValue (*volumeSpec, 0.5), WithinAbs (0.5, 1e-9));
}

TEST_CASE ("a muted playlist track's automation does nothing", "[automation][render]")
{
    // A muted lane silences its notes; it has to silence what its automation
    // does too, or a muted track still moves the mix.
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"), &undo);
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

TEST_CASE ("an automation pointing at something deleted is dropped with a warning", "[automation]")
{
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    const auto name = channel[ids::name].toString();

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, name + " > Volume"), &undo);
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0, 2, &undo);

    juce::StringArray warnings;
    REQUIRE (buildSnapshot (project, &warnings).anyAutomation);
    REQUIRE (warnings.isEmpty());

    ProjectEdits::removeChannel (project, channel, &undo);

    warnings.clear();
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (! warnings.isEmpty());
    REQUIRE (warnings.joinIntoString (" ").contains ("no longer exists"));
    REQUIRE (! snapshot.anyAutomation);
}

TEST_CASE ("removing an automation removes the clips that used it", "[automation]")
{
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"), &undo);
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);

    const auto countClips = [&track]
    {
        int n = 0;

        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP))
                ++n;

        return n;
    };

    const auto before = countClips();
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0, 2, &undo);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 4, 2, &undo);
    REQUIRE (countClips() == before + 2);

    undo.beginNewTransaction ("Remove automation");
    REQUIRE (ProjectEdits::removeAutomation (project, automation, &undo));
    REQUIRE (countClips() == before);

    // And it is one undo step, like every other cascading removal.
    REQUIRE (undo.undo());
    REQUIRE (countClips() == before + 2);
}

TEST_CASE ("automation survives save and load", "[automation][schema]")
{
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"), &undo);
    setCurve (automation, { { 0.0, 0.1 }, { 24.0, 0.9 } }, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 1, 3, &undo);

    const auto loaded = ProjectSerializer::fromJsonString (ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());
    REQUIRE (loaded.tree.isEquivalentTo (project));

    const auto reloaded = ProjectEdits::findAutomation (loaded.tree, (int) automation[ids::id]);
    REQUIRE (reloaded.isValid());
    REQUIRE_THAT (ProjectEdits::automationValueAt (reloaded, 12.0),
                  WithinAbs (ProjectEdits::automationValueAt (automation, 12.0), 1e-9));
}

TEST_CASE ("a stepped segment holds its left value until the next point", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);
    setCurve (automation, { { 0.0, 0.2 }, { 16.0, 0.8 } }, &undo);

    auto first = ProjectEdits::sortedAutomationPoints (automation).getFirst();
    REQUIRE (first.isValid());

    // As a curve with no bend it is a ramp: halfway across is halfway between.
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.5, 1e-9));

    ProjectEdits::setPointShape (first, SegmentShape::step, &undo);

    // Stepped, every step of the segment reads the LEFT value - right up to but
    // not including the next point, where it jumps.
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 0.0),  WithinAbs (0.2, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0),  WithinAbs (0.2, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 15.9), WithinAbs (0.2, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 16.0), WithinAbs (0.8, 1e-9));

    // A step IGNORES the bend rather than losing it, so the shape is reversible.
    ProjectEdits::setPointCurve (first, 0.6, &undo);
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.2, 1e-9));

    ProjectEdits::setPointShape (first, SegmentShape::curve, &undo);
    REQUIRE_THAT ((double) first[ids::curve], WithinAbs (0.6, 1e-9));

    // And "Line" is that same shape with the bend flattened, in one write.
    ProjectEdits::setPointStraight (first, &undo);
    REQUIRE_THAT ((double) first[ids::curve], WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.5, 1e-9));
}

TEST_CASE ("a point added to a staircase does not put a ramp in it", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);
    setCurve (automation, { { 0.0, 0.0 }, { 16.0, 1.0 } }, &undo);

    ProjectEdits::setPointShape (ProjectEdits::sortedAutomationPoints (automation).getFirst(),
                                 SegmentShape::step, &undo);

    auto added = ProjectEdits::addAutomationPoint (automation, 8.0, 0.5, &undo);
    REQUIRE (added.isValid());
    REQUIRE (added[ids::shape].toString() == "step");
}

TEST_CASE ("a file from before shapes loads as the line it drew", "[automation][schema]")
{
    // The additive-default claim, asserted rather than argued: a point with no
    // `shape` key is a curve with whatever bend it had, which for every file
    // written before shapes existed is a bend of zero - a straight line.
    const juce::String older = R"({
      "format": "dew-project",
      "formatVersion": 10,
      "name": "Older project",
      "tempoBpm": 120.0, "stepsPerBeat": 4, "beatsPerBar": 4, "beatUnit": 4, "barsInSong": 4,
      "channels": [ { "id": 1, "name": "Kick", "mixerTrackId": 1 } ],
      "patterns": [ { "id": 1, "name": "Pattern 1", "lengthSteps": 16 } ],
      "automations": [ { "id": 1, "name": "Kick > Volume", "scope": "channel",
                         "targetId": 1, "slot": -1, "param": "volume",
                         "points": [ { "step": 0.0, "value": 0.0 },
                                     { "step": 16.0, "value": 1.0 } ] } ],
      "playlist": { "tracks": [ { "name": "Track 1" } ] },
      "mixer": { "master": { "gain": 1.0 }, "tracks": [ { "id": 1, "name": "Insert 1" } ] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (older);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto automation = ProjectEdits::findAutomation (loaded.tree, 1);
    REQUIRE (automation.isValid());

    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
            REQUIRE (point[ids::shape].toString() == "curve");

    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.5, 1e-9));
}

TEST_CASE ("the editor and the engine agree about every point of a curve", "[automation]")
{
    // The two evaluators were hand-copied bodies of the same arithmetic in two
    // layers - ProjectEdits for the editor and the tests, AutomationSnapshot for
    // the audio thread - and nothing compared them. This is what would have
    // caught them drifting, and it is what keeps a segment shape from being
    // added to one and not the other.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"), &undo);
    setCurve (automation, { { 0.0, 0.1 }, { 9.0, 0.85 }, { 20.0, 0.4 }, { 33.0, 1.0 } }, &undo);

    // A bend on the second segment, so the comparison covers the branch a
    // straight line does not reach.
    for (auto point : automation)
        if (point.hasType (ids::POINT) && juce::approximatelyEqual ((double) point[ids::step], 9.0))
            point.setProperty (ids::curve, 0.7, &undo);

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (snapshot.automations.size() == 1);

    const auto& engine = snapshot.automations.front();

    // The engine reports the parameter's own units, so the editor's 0..1 goes
    // through the same mapping before they are compared - that mapping is not
    // what is under test here, the interpolation is.
    const AutomationParamSpec spec { nullptr, "", (double) engine.minimum,
                                     (double) engine.maximum, false, engine.logarithmic };

    for (int i = 0; i <= 200; ++i)
    {
        const auto step = -5.0 + 45.0 * (double) i / 200.0;

        INFO ("step " << step);
        REQUIRE_THAT ((double) engine.valueAt (step),
                      WithinAbs (mapAutomationValue (spec,
                                                     ProjectEdits::automationValueAt (automation, step)),
                                 1e-5));
    }
}

TEST_CASE ("a point dragged between two steps survives save and load", "[automation][schema]")
{
    // The test above round-trips a curve whose points sit on whole steps, which
    // is the one case the bug could not reach: `step` was declared an int in
    // pointSpec, and coerceToTypeOf drives its conversion off the runtime type
    // of the declared default - so every point a DRAG produced was truncated
    // back to the last whole step on the way out, silently, and the curve
    // reloaded as a shape nobody drew.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);
    setCurve (automation, { { 0.0, 0.0 }, { 6.5, 0.5 }, { 13.25, 1.0 } }, &undo);

    const auto loaded = ProjectSerializer::fromJsonString (ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());

    const auto reloaded = ProjectEdits::findAutomation (loaded.tree, (int) automation[ids::id]);
    REQUIRE (reloaded.isValid());

    juce::Array<double> steps;

    for (const auto& point : reloaded)
        if (point.hasType (ids::POINT))
            steps.add ((double) point[ids::step]);

    REQUIRE (steps.size() == 3);
    REQUIRE_THAT (steps[1], WithinAbs (6.5, 1e-9));
    REQUIRE_THAT (steps[2], WithinAbs (13.25, 1e-9));
}

TEST_CASE ("the target list covers channels, effects, tracks and master", "[automation]")
{
    auto project = ProjectFactory::createDemo();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    ProjectEdits::addEffect (project, channel, "delay", &undo);

    juce::StringArray names;

    for (const auto& target : availableAutomationTargets (project))
        names.add (target.displayName);

    const auto name = channel[ids::name].toString();

    REQUIRE (names.contains (name + " > Volume"));
    REQUIRE (names.contains (name + " > Pan"));
    REQUIRE (names.contains (name + " > Delay > Time"));
    REQUIRE (names.contains (name + " > Delay > Feedback"));
    REQUIRE (names.contains ("Insert 1 > Gain"));
    REQUIRE (names.contains ("Master > Gain"));

    // Nothing that is not a continuous quantity: no names, no lengths, no ids.
    for (const auto& target : availableAutomationTargets (project))
    {
        REQUIRE (target.maximum > target.minimum);
        REQUIRE (target.property != ids::name);
        REQUIRE (target.property != ids::id);
    }
}

