#include <utility>

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

TEST_CASE ("a point cannot be dragged past its neighbours", "[automation]")
{
    // The old name said "points stay in step order HOWEVER they are dragged",
    // which described the mechanism: the point went wherever it was dragged and
    // the tree was re-sorted behind it. That shuffled the children under the
    // point being dragged and put a moveChild on the undo stack for every frame.
    // The rule now is that the drag stops.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);

    auto a = ProjectEdits::addAutomationPoint (automation, 4.0, 0.2, &undo);
    ProjectEdits::addAutomationPoint (automation, 8.0, 0.8, &undo);

    const auto indexBefore = automation.indexOf (a);

    // Drag the earlier point far past the later one.
    ProjectEdits::moveAutomationPoint (automation, a, 20.0, 0.2, &undo);

    // It moved as far as it was allowed and no further.
    REQUIRE ((double) a[ids::step] > 4.0);
    REQUIRE_THAT ((double) a[ids::step], WithinAbs (8.0 - ProjectEdits::minPointGap, 1e-9));

    // And nothing was reordered - which is what removing the re-sort buys, and
    // what nothing else asserts.
    REQUIRE (automation.indexOf (a) == indexBefore);

    double previous = -1.0;

    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
        {
            REQUIRE ((double) point[ids::step] >= previous);
            previous = (double) point[ids::step];
        }

    // Backwards too, and the first point still stops at zero rather than going
    // negative.
    ProjectEdits::moveAutomationPoint (automation, a, -50.0, 0.2, &undo);
    REQUIRE_THAT ((double) a[ids::step], WithinAbs (ProjectEdits::minPointGap, 1e-9));
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

TEST_CASE ("a discrete curve never lands between two states", "[automation]")
{
    // What makes a stepped target one model with a continuous one rather than a
    // special case: the SNAP is in the mapping every consumer already calls, so
    // a curve over a filter mode is a curve over the mode index and cannot ask
    // for a mode that does not exist.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    auto filter = ProjectEdits::addEffect (project, channel, "filter", &undo);
    REQUIRE (filter.isValid());

    const auto* spec = findParamSpec (AutomationScope::channelEffect, "filter", ids::filterMode);
    REQUIRE (spec != nullptr);
    REQUIRE (spec->automatable);
    REQUIRE (spec->numDiscreteValues() == 3);

    for (int i = 0; i <= 200; ++i)
    {
        const auto normalised = (double) i / 200.0;
        const auto value = automationValueFor (*spec, normalised);

        INFO ("at " << normalised << " the mode index is " << value);
        REQUIRE ((juce::approximatelyEqual (value, 0.0)
                  || juce::approximatelyEqual (value, 1.0)
                  || juce::approximatelyEqual (value, 2.0)));
    }

    // Equal-width buckets, so the ends and the middle are the three modes.
    REQUIRE_THAT (automationValueFor (*spec, 0.0),  WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (automationValueFor (*spec, 0.5),  WithinAbs (1.0, 1e-9));
    REQUIRE_THAT (automationValueFor (*spec, 1.0),  WithinAbs (2.0, 1e-9));

    // And a continuous neighbour in the same table is untouched by the snap.
    const auto* cutoff = findParamSpec (AutomationScope::channelEffect, "filter", ids::cutoff);
    REQUIRE (cutoff != nullptr);
    REQUIRE (automationValueFor (*cutoff, 0.5) > cutoff->minimum);
    REQUIRE (automationValueFor (*cutoff, 0.5) < cutoff->maximum);
}

TEST_CASE ("a fresh curve over a discrete target is stepped", "[automation]")
{
    // A ramp between two states of a toggle is a shape nobody meant to draw, so
    // a bypass lane looks like one the moment it exists rather than after a trip
    // to the shape menu.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto continuous = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"), &undo);

    for (const auto& point : ProjectEdits::sortedAutomationPoints (continuous))
        REQUIRE (point[ids::shape].toString() == "curve");

    auto stepped = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Mute"), &undo);

    for (const auto& point : ProjectEdits::sortedAutomationPoints (stepped))
        REQUIRE (point[ids::shape].toString() == "step");
}

TEST_CASE ("a mute curve silences a channel and lets it back in", "[automation][render]")
{
    // Compared against the SAME project without the clip, not against its own
    // second half: a demo that happened to get louder would pass that on its own.
    auto project = ProjectFactory::createDemo();
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
    auto automation = ProjectEdits::addAutomation (project,
                                                   targetNamed (project,
                                                                first[ids::name].toString() + " > Mute"),
                                                   &undo);
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

TEST_CASE ("frequency parameters sweep by ear, not by hertz", "[automation]")
{
    // A cutoff mapped linearly spends four fifths of a drawn curve above 3kHz.
    auto project = ProjectFactory::createDefault();

    const auto cutoffSpec = findParamSpec (AutomationScope::channelEffect, "filter", ids::cutoff);
    REQUIRE (cutoffSpec != nullptr);
    REQUIRE ((cutoffSpec->curve == ParamCurve::logarithmic));

    // Halfway up the curve is the geometric middle of the range, which is
    // roughly where a listener would put "halfway".
    const auto middle = automationValueFor (*cutoffSpec, 0.5);
    INFO ("midpoint " << middle);
    REQUIRE (middle > 400.0);
    REQUIRE (middle < 1200.0);

    // The ends still land exactly on the range.
    REQUIRE_THAT (automationValueFor (*cutoffSpec, 0.0), WithinAbs (cutoffSpec->minimum, 1e-6));
    REQUIRE_THAT (automationValueFor (*cutoffSpec, 1.0), WithinAbs (cutoffSpec->maximum, 1e-3));

    // Gain-like parameters stay linear: half volume means half.
    const auto volumeSpec = findParamSpec (AutomationScope::channel, "", ids::volume);
    REQUIRE (volumeSpec != nullptr);
    REQUIRE (volumeSpec->curve == ParamCurve::linear);
    REQUIRE_THAT (automationValueFor (*volumeSpec, 0.5), WithinAbs (0.5, 1e-9));
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
    REQUIRE (engine.spec != nullptr);

    // The engine reports the parameter's own units, so the editor's 0..1 goes
    // through the same mapping before they are compared - that mapping is not
    // what is under test here, the interpolation is.
    const auto& spec = *engine.spec;

    for (int i = 0; i <= 200; ++i)
    {
        const auto step = -5.0 + 45.0 * (double) i / 200.0;

        INFO ("step " << step);
        REQUIRE_THAT ((double) engine.valueAt (step),
                      WithinAbs (automationValueFor (spec,
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

TEST_CASE ("every target the picker offers is one a control could ask for", "[automation]")
{
    // Both directions of one fact. The owner-node -> (scope, targetId, slot)
    // mapping used to live only inside the picker's own loop, so a knob had
    // nowhere to ask what it drove - which is why automation was reachable from
    // one button and not from the control itself. The picker is now a walk over
    // automationTargetFor, and this is what keeps the claim honest.
    auto project = ProjectFactory::createDemo();

    const auto offered = availableAutomationTargets (project);
    REQUIRE (offered.size() > 10);

    /** The node a target names, found the long way round - so this test does not
        share the walk it is checking. */
    const auto nodeFor = [&project] (const AutomationTarget& target) -> juce::ValueTree
    {
        const auto nth = [] (const juce::ValueTree& parent, const juce::Identifier& type, int index)
        {
            int i = 0;

            for (const auto& child : parent)
                if (child.hasType (type) && i++ == index)
                    return child;

            return juce::ValueTree();
        };

        const auto mixer = project.getChildWithName (ids::MIXER);

        if (target.scope == AutomationScope::master)
            return mixer.getChildWithName (ids::MASTER);

        // The arrangement itself, which is the project's own root.
        if (target.scope == AutomationScope::project)
            return project;

        juce::ValueTree owner;

        for (const auto& child : target.scope == AutomationScope::mixerTrack
                                 || target.scope == AutomationScope::mixerEffect
                                     ? mixer : project)
            if (child.hasType (target.scope == AutomationScope::mixerTrack
                               || target.scope == AutomationScope::mixerEffect
                                   ? ids::MIXER_TRACK : ids::CHANNEL)
                && (int) child[ids::id] == target.targetId)
                owner = child;

        switch (target.scope)
        {
            case AutomationScope::channel:
            case AutomationScope::mixerTrack:
                return owner;

            case AutomationScope::channelOsc:
                return nth (owner.getChildWithName (ids::INSTRUMENT), ids::OSC, target.slot);

            case AutomationScope::channelEffect:
            case AutomationScope::mixerEffect:
                return nth (owner, ids::EFFECT, target.slot);

            case AutomationScope::project:
            case AutomationScope::master:
                break;
        }

        return {};
    };

    for (const auto& target : offered)
    {
        INFO ("target " << target.displayName);

        const auto node = nodeFor (target);
        REQUIRE (node.isValid());

        const auto resolved = automationTargetFor (project, node, target.property);
        REQUIRE (resolved.has_value());

        CHECK (resolved->scope == target.scope);
        CHECK (resolved->targetId == target.targetId);
        CHECK (resolved->slot == target.slot);
        CHECK (resolved->property == target.property);
        CHECK (resolved->displayName == target.displayName);
        CHECK (resolved->spec == target.spec);
    }
}

TEST_CASE ("a node with nothing to automate resolves to nothing", "[automation]")
{
    // The negatives matter as much: a resolver that said yes to everything would
    // put a "create automation clip" item on every control in the application.
    auto project = ProjectFactory::createDefault();

    const auto channel = project.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());

    // A property that exists on the node but is not automatable.
    REQUIRE_FALSE (automationTargetFor (project, channel, ids::basePitch).has_value());
    REQUIRE_FALSE (automationTargetFor (project, channel, ids::name).has_value());

    // An envelope stage: not a quantity you move THROUGH a note.
    const auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);
    REQUIRE (amp.isValid());
    REQUIRE_FALSE (automationTargetFor (project, amp, ids::attack).has_value());

    // A pattern, and a playlist track - which carries no id to point at.
    REQUIRE_FALSE (automationTargetFor (project, project.getChildWithName (ids::PATTERN),
                                        ids::lengthSteps).has_value());

    const auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    REQUIRE (track.isValid());
    REQUIRE_FALSE (automationTargetFor (project, track, ids::mute).has_value());

    // And an invalid node, which is what a control on a deleted channel hands in.
    REQUIRE_FALSE (automationTargetFor (project, {}, ids::volume).has_value());
}

TEST_CASE ("a classic oscillator slot offers nothing to automate", "[automation]")
{
    // A wave position on a slot that is not running a wavetable would be a
    // control that silently did nothing. The picker has always skipped it; the
    // resolver has to agree, or a right-click would offer what the picker does
    // not.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    auto osc = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::OSC);
    REQUIRE (osc.isValid());

    ProjectEdits::setProperty (osc, ids::mode, "classic", &undo, "Classic");
    REQUIRE_FALSE (automationTargetFor (project, osc, ids::wavePosition).has_value());

    ProjectEdits::setProperty (osc, ids::mode, "wavetable", &undo, "Wavetable");
    REQUIRE (automationTargetFor (project, osc, ids::wavePosition).has_value());
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
        REQUIRE (target.spec != nullptr);
        REQUIRE (target.spec->maximum > target.spec->minimum);
        REQUIRE (target.spec->automatable);
        REQUIRE (target.property != ids::name);
        REQUIRE (target.property != ids::id);
    }
}

