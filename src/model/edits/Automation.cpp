// =============================================================================
// Automation lanes, their points, and the shape between them.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// The largest of the seven, and the one with the most invariants: points
// stay sorted by step, a point cannot pass its neighbour, and a curve is
// stored as a bend on the point to its LEFT.
// =============================================================================

#include "model/ProjectEdits.h"

#include <cmath>

#include "model/AutomationCurve.h"
#include "model/Ids.h"
#include "model/TreeWalk.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

const NodeSpec& automationSpecFor()
{
    return childSpecFor (projectSpec(), "automations");
}

const NodeSpec& pointSpecFor()
{
    return childSpecFor (automationSpecFor(), "points");
}

/** Points in step order. The tree keeps them sorted, but a file could not, and
    evaluating an unsorted curve produces a shape nobody drew.
*/
juce::Array<juce::ValueTree> sortedPoints (const juce::ValueTree& automation)
{
    juce::Array<juce::ValueTree> points;

    for (const auto& child : automation)
        if (child.hasType (ids::POINT))
            points.add (child);

    std::stable_sort (points.begin(), points.end(),
                      [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (double) a[ids::step] < (double) b[ids::step]; });

    return points;
}

} // namespace

juce::ValueTree ProjectEdits::addAutomation (juce::ValueTree project,
                                             const AutomationTarget& target,
                                             juce::UndoManager* undo)
{
    auto automation = defaultTreeFor (automationSpecFor());

    automation.setProperty (ids::id, nextFreeId (project, ids::AUTOMATION), nullptr);
    automation.setProperty (ids::name, target.displayName, nullptr);
    automation.setProperty (ids::scope, automationScopeToString (target.scope), nullptr);
    automation.setProperty (ids::targetId, target.targetId, nullptr);
    automation.setProperty (ids::slot, target.slot, nullptr);
    automation.setProperty (ids::param, target.property.toString(), nullptr);

    // Two points, so a new clip is a line you can grab rather than an empty
    // rectangle that does nothing until you guess how to start it.
    //
    // Stepped when the target is discrete: a ramp between two states of a toggle
    // is a shape nobody meant to draw, and a bypass lane should look like a
    // bypass lane the moment it exists rather than after a trip to the menu.
    const auto discrete = target.spec != nullptr && target.spec->isDiscrete();

    for (const auto step : { 0.0, 16.0 })
    {
        auto point = defaultTreeFor (pointSpecFor());
        point.setProperty (ids::step, step, nullptr);
        point.setProperty (ids::value, 0.5, nullptr);

        if (discrete)
            point.setProperty (ids::shape, segmentShapeToString (SegmentShape::step), nullptr);

        automation.appendChild (point, nullptr);
    }

    int insertAt = project.getNumChildren();

    for (int i = 0; i < project.getNumChildren(); ++i)
        if (project.getChild (i).hasType (ids::PATTERN)
            || project.getChild (i).hasType (ids::AUTOMATION))
            insertAt = i + 1;

    project.addChild (automation, insertAt, undo);
    return automation;
}

juce::ValueTree ProjectEdits::addAutomationWithClip (juce::ValueTree project,
                                                     const AutomationTarget& target, int startStep,
                                                     int lengthSteps, juce::UndoManager* undo)
{
    auto automation = addAutomation (project, target, undo);

    if (! automation.isValid())
        return {};

    auto playlist = project.getChildWithName (ids::PLAYLIST);

    for (auto track : playlist)
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        if (findClipAtStep (track, startStep).isValid())
            continue;

        auto clip = addAutomationClip (track, (int) automation[ids::id], startStep, lengthSteps,
                                       undo);
        growSongToFitClips (project, undo);
        return clip;
    }

    // Every lane is taken at that step, so make one. The alternative this used to
    // choose - undo the definition and return nothing - was a menu item that
    // did nothing at all, which is the worse of the two by a distance now that
    // every control offers it.
    auto track = addPlaylistTrack (
        project,
        tr (StringId::project_trackN, Args {}.with ("number", playlist.getNumChildren() + 1)),
        undo);

    if (! track.isValid())
        return {};

    auto clip = addAutomationClip (track, (int) automation[ids::id], startStep, lengthSteps, undo);
    growSongToFitClips (project, undo);
    return clip;
}

juce::ValueTree ProjectEdits::findAutomation (const juce::ValueTree& project, int automationId)
{
    return tree::childWithId (project, ids::AUTOMATION, automationId);
}

bool ProjectEdits::removeAutomation (juce::ValueTree project, juce::ValueTree automation,
                                     juce::UndoManager* undo)
{
    const auto index = project.indexOf (automation);

    if (index < 0)
        return false;

    const auto automationId = (int) automation[ids::id];

    // Same rule as removePattern: a clip referring to something that no longer
    // exists is dropped here, so the whole removal is one undo step.
    for (auto track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (int i = track.getNumChildren(); --i >= 0;)
        {
            const auto clip = track.getChild (i);

            if (clip.hasType (ids::CLIP) && isAutomationClip (clip)
                && (int) clip[ids::automationId] == automationId)
                track.removeChild (i, undo);
        }
    }

    project.removeChild (index, undo);
    return true;
}

juce::ValueTree ProjectEdits::addAutomationPoint (juce::ValueTree automation, double step,
                                                  double value, juce::UndoManager* undo)
{
    if (! automation.isValid())
        return {};

    const auto clampedStep = juce::jmax (0.0, step);
    const auto clampedValue = juce::jlimit (0.0, 1.0, value);

    // Two points on one step is a curve with no defined value there, so an
    // existing one moves instead of being joined.
    // The same gap moveAutomationPoint clamps to. Anything closer than that is
    // the same point: a pair inside the gap could never be separated again,
    // because the drag rule would not let either of them move past the other.
    for (auto existing : sortedPoints (automation))
        if (std::abs ((double) existing[ids::step] - clampedStep) < minPointGap)
        {
            existing.setProperty (ids::value, clampedValue, undo);
            return existing;
        }

    auto point = defaultTreeFor (pointSpecFor());
    point.setProperty (ids::step, clampedStep, nullptr);
    point.setProperty (ids::value, clampedValue, nullptr);

    // Inherit the shape of the segment being split, rather than taking the
    // default. A curve drawn as steps stays steps when a point is added to it;
    // otherwise adding one silently puts a ramp in the middle of a staircase.
    for (const auto& existing : sortedPoints (automation))
    {
        if ((double) existing[ids::step] > clampedStep)
            break;

        point.setProperty (ids::shape, existing[ids::shape], nullptr);
    }

    int insertAt = automation.getNumChildren();

    for (int i = 0; i < automation.getNumChildren(); ++i)
        if (automation.getChild (i).hasType (ids::POINT)
            && (double) automation.getChild (i)[ids::step] > clampedStep)
        {
            insertAt = i;
            break;
        }

    automation.addChild (point, insertAt, undo);
    return point;
}

void ProjectEdits::moveAutomationPoint (juce::ValueTree automation, juce::ValueTree point,
                                        double step, double value, juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    // Clamped between the neighbours rather than let past them and re-sorted.
    //
    // Re-sorting worked, but it shuffled the tree under the point being dragged
    // - so a drag was a sequence of moveChild calls on the undo stack, and
    // anything holding the point's index had it change mid-gesture. Stopping the
    // point is also what the hand expects: a curve's points are in an order, and
    // a drag should meet that order rather than silently rewrite it.
    auto lower = 0.0;
    auto upper = std::numeric_limits<double>::max();

    const auto sorted = sortedPoints (automation);
    const auto index = sorted.indexOf (point);

    if (index > 0)
        lower = (double) sorted[index - 1][ids::step] + minPointGap;

    if (index >= 0 && index + 1 < sorted.size())
        upper = (double) sorted[index + 1][ids::step] - minPointGap;

    // A curve squeezed until its neighbours cross would otherwise invert the
    // range and jump the point to the far side of them.
    const auto clamped = upper > lower ? juce::jlimit (lower, upper, juce::jmax (0.0, step))
                                       : juce::jmax (0.0, lower);

    point.setProperty (ids::step, clamped, undo);
    point.setProperty (ids::value, juce::jlimit (0.0, 1.0, value), undo);
}

void ProjectEdits::removeAutomationPoint (juce::ValueTree automation, juce::ValueTree point,
                                          juce::UndoManager* undo)
{
    // A curve with fewer than two points has no shape to draw or evaluate.
    if (sortedPoints (automation).size() <= 2)
        return;

    const auto index = automation.indexOf (point);

    if (index >= 0)
        automation.removeChild (index, undo);
}

juce::Array<juce::ValueTree>
ProjectEdits::sortedAutomationPoints (const juce::ValueTree& automation)
{
    return sortedPoints (automation);
}

void ProjectEdits::setPointShape (juce::ValueTree point, SegmentShape shape,
                                  juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    // The shape ONLY. A stepped segment ignores the bend rather than losing it,
    // so switching to step and back returns the curve you had - which is what
    // makes the three menu items reversible.
    point.setProperty (ids::shape, segmentShapeToString (shape), undo);
}

void ProjectEdits::setColour (juce::ValueTree node, const juce::String& hex,
                              juce::UndoManager* undo, bool continuingTransaction)
{
    if (! node.isValid())
        return;

    setProperty (node, ids::colour, hex, undo, "Change colour", continuingTransaction);
}

void ProjectEdits::setPointStraight (juce::ValueTree point, juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    // What the editor's "Line" means, in one place and one undo step. A line is
    // a curve with no bend, so it is these two writes and not a third stored
    // shape - which would be a second place holding the same fact as
    // `curve == 0`, free to disagree with the bend beside it.
    point.setProperty (ids::shape, segmentShapeToString (SegmentShape::curve), undo);
    point.setProperty (ids::curve, 0.0, undo);
}

void ProjectEdits::setPointCurve (juce::ValueTree point, double bend, juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    point.setProperty (ids::curve, juce::jlimit (-1.0, 1.0, bend), undo);
}

double ProjectEdits::automationValueAt (const juce::ValueTree& automation, double step)
{
    // Delegates rather than evaluating. This body and AutomationSnapshot::valueAt
    // were the same arithmetic written twice, in two layers, and the playlist's
    // painter was a third place that implemented neither - so a bend was heard
    // and drawn straight, and a segment shape would have had to be added to
    // three places already free to disagree.
    const auto points = curvePointsOf (automation);

    return curveValueAt (points, step);
}

} // namespace dew
