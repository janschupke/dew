#include "control/OpsSupport.h"
#include "model/Meter.h"
#include "control/ParamAddress.h"
#include "model/AutomationCurve.h"

namespace dew::control
{

namespace
{

/** Every parameter in the project a curve can be pointed at.

    A walk over availableAutomationTargets, reported in the SAME address
    vocabulary params_write takes, so a caller that has just found a parameter
    can automate it without translating anything. That the two spellings line up
    is stated once, in automationScopeOf.
*/
ControlResult listTargets (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto filter = textArg (args, "contains");

    juce::Array<juce::var> rows;

    for (const auto& target : availableAutomationTargets (project))
    {
        if (filter.isNotEmpty() && ! target.displayName.containsIgnoreCase (filter))
            continue;

        const auto address = addressOfTarget (target);

        rows.add (Obj {}
                      .set ("displayName", target.displayName)
                      .set ("target", address.target)
                      .set ("id", address.id)
                      .set ("group", address.group)
                      .set ("slot", address.slot)
                      .set ("param", address.param.toString()));
    }

    return ControlResult::success (Obj {}.set ("targets", arrayOf (rows)));
}

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto address = addressFrom (args);
    const auto scope = automationScopeOf (address);

    if (! scope.has_value())
        return ControlResult::failure (
            address.describe()
            + " cannot be automated: automation has no scope for it. Call "
              "automation_targets_list for what can.");

    const auto node = paramNodeFor (project, address);

    if (! node.isValid())
        return ControlResult::failure ("nothing is addressed by " + address.describe() + ".");

    // automationTargetFor is the primitive, and going through it is what makes
    // a curve created here identical to one created by right-clicking the knob
    // - same scope, same slot, same display name, same spec.
    const auto target = automationTargetFor (project, node, address.param);

    if (! target.has_value())
        return ControlResult::failure (
            address.param.toString() + " on " + address.describe()
            + " is not automatable. ParamSpec::automatable is the only gate, and "
              "params_list reports it per parameter.");

    auto* undo = host.undoManager();

    const auto withClip = flagArg (args, "placeClip", true);

    // The op's arguments stay in BARS - a curve is placed where a section is,
    // and a section is a bar - and the model stores steps, so the conversion
    // happens once, here.
    const auto stepsPerBar = juce::jmax (1, Meter::of (project).stepsPerBar());

    // Creating the definition AND placing a clip is one call in the model
    // precisely so it is one undo step: a curve with nowhere to play is a
    // curve nothing hears.
    const auto created = withClip
                             ? ProjectEdits::addAutomationWithClip (
                                   project, *target, intArg (args, "startBar") * stepsPerBar,
                                   juce::jmax (1, intArg (args, "lengthBars", 1)) * stepsPerBar,
                                   undo)
                             : ProjectEdits::addAutomation (project, *target, undo);

    if (! created.isValid())
        return ControlResult::failure ("the automation could not be created.");

    // addAutomationWithClip returns the CLIP, not the automation - it was
    // written for a caller that wanted somewhere to scroll to. A clip carries
    // no id of its own, so reading one off it answered 0 to every caller. What
    // a client needs is the AUTOMATION's id, because that is what
    // automation_points_write takes, so it is resolved back through the clip.
    const auto automation = withClip ? ProjectEdits::findAutomation (
                                           project, (int) created[ids::automationId])
                                     : created;

    if (! automation.isValid())
        return ControlResult::failure ("the automation was placed but could not be found again.");

    host.flushEngine();

    return ControlResult::success (Obj {}
                                       .set ("id", (int) automation[ids::id])
                                       .set ("name", automation[ids::name].toString())
                                       .set ("displayName", target->displayName));
}

ControlResult remove (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    juce::Array<juce::ValueTree> doomed;

    for (const auto& id : arrayArg (args, "ids"))
    {
        const auto automation = ProjectEdits::findAutomation (project, (int) id);

        if (! automation.isValid())
            return ControlResult::failure ("no automation with id " + juce::String ((int) id)
                                           + ".");

        doomed.add (automation);
    }

    auto removed = 0;

    for (const auto& automation : doomed)
        if (ProjectEdits::removeAutomation (project, automation, host.undoManager()))
            ++removed;

    host.flushEngine();

    return applied (removed, doomed.size() - removed);
}

ControlResult writePoints (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    auto automation = ProjectEdits::findAutomation (project, intArg (args, "id"));

    if (! automation.isValid())
        return ControlResult::failure ("no automation with id " + juce::String (intArg (args, "id"))
                                       + ".");

    const auto& entries = arrayArg (args, "points");

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);
        const auto shape = textArg (entry, "shape", "curve");

        if (shape != "curve" && shape != "step" && shape != "line")
            return ControlResult::failure ("points[" + juce::String (i) + "] '" + shape
                                           + "' is not a segment shape. Use line, curve or step.");
    }

    auto* undo = host.undoManager();

    // The originals, taken BEFORE anything is written.
    //
    // `replace` cannot be "remove them, then add": removeAutomationPoint
    // refuses to take a curve below two points, because a curve with fewer has
    // no shape to draw or evaluate - so clearing first removes nothing at all
    // and the new points land on top of the old ones. Writing first and then
    // removing what is left over is the order that works, and it also handles
    // the case addAutomationPoint creates: a new point at an existing point's
    // step MOVES that point rather than adding one, so the original is now one
    // of the caller's and must not be removed.
    const auto replacing = flagArg (args, "replace");
    const auto originals = replacing ? ProjectEdits::sortedAutomationPoints (automation)
                                     : juce::Array<juce::ValueTree> {};

    juce::Array<juce::ValueTree> written;

    for (const auto& entry : entries)
    {
        // Values are 0..1 and are mapped onto the parameter's own units by the
        // one function the picker, the editor, the painter and the engine all
        // call - which is what keeps "what you draw is what you hear" true for
        // a toggle as well as for a cutoff.
        const auto point = ProjectEdits::addAutomationPoint (
            automation, numberArg (entry, "step"),
            juce::jlimit (0.0, 1.0, numberArg (entry, "value")), undo);

        if (! point.isValid())
            continue;

        const auto shape = textArg (entry, "shape", "curve");

        if (shape == "line")
        {
            // "Line" is shape `curve` with no bend, and is set through the one
            // function that says so - a stored `line` would be a second place
            // holding the same fact as `curve == 0`, free to disagree with the
            // bend beside it.
            ProjectEdits::setPointStraight (point, undo);
        }
        else
        {
            ProjectEdits::setPointShape (
                point, shape == "step" ? SegmentShape::step : SegmentShape::curve, undo);

            if (hasArg (entry, "bend"))
                ProjectEdits::setPointCurve (point, numberArg (entry, "bend"), undo);
        }

        written.add (point);
    }

    for (const auto& original : originals)
        if (! written.contains (original))
            ProjectEdits::removeAutomationPoint (automation, original, undo);

    host.flushEngine();

    return ControlResult::success (
        Obj {}
            .set ("applied", written.size())
            .set ("points", ProjectEdits::sortedAutomationPoints (automation).size()));
}

ControlResult readCurve (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto automation = ProjectEdits::findAutomation (project, intArg (args, "id"));

    if (! automation.isValid())
        return ControlResult::failure ("no automation with id " + juce::String (intArg (args, "id"))
                                       + ".");

    juce::Array<juce::var> points;

    for (const auto& point : ProjectEdits::sortedAutomationPoints (automation))
        points.add (Obj {}
                        .set ("step", (double) point[ids::step])
                        .set ("value", (double) point[ids::value])
                        .set ("bend", (double) point[ids::curve])
                        .set ("shape", point[ids::shape].toString()));

    const auto* spec = specForAutomation (project, automation);

    return ControlResult::success (Obj {}
                                       .set ("id", (int) automation[ids::id])
                                       .set ("name", automation[ids::name].toString())
                                       .set ("scope", automation[ids::scope].toString())
                                       .set ("param", automation[ids::param].toString())
                                       .set ("stale", spec == nullptr)
                                       .set ("points", arrayOf (points)));
}

} // namespace

void appendAutomationOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "automation_targets_list",
          OpScope::read,
          OpEdits::no,
          "List every parameter in this project that a curve can be pointed at.",
          "Automation is a curated set, not every property: whether a parameter is worth "
          "a curve is declared beside the parameter itself. A relation between tracks "
          "(solo), a read-pointer discontinuity (a sample's reverse and loop) and a "
          "pattern's length are all deliberately absent, each for its own reason.\n\n"
          "Answers in the same five-field address params_write takes, so a parameter you "
          "have just set can be automated without translating anything.",
          { { "contains", ValueKind::text, false,
              "Only targets whose display name contains this." } },
          listTargets });

    all.push_back (
        { "automation_write",
          OpScope::write,
          OpEdits::yes,
          "Create an automation curve for a parameter, and place a clip for it.",
          "Creating the curve and placing its clip is one call because it is one action: "
          "a curve with nowhere to play is a curve nothing hears. The clip lands on the "
          "first lane with room at `startBar`, and a lane is added if every one is "
          "occupied there.\n\n"
          "A fresh curve has two points, so it is a line rather than an empty box. Shape "
          "it with automation_points_write.\n\n"
          "Refuses a parameter that is not automatable, and says so - which is a "
          "different answer from a parameter that does not exist.",
          { { "target", ValueKind::text, true, "project, channel, mixerTrack or master." },
            { "id", ValueKind::integer, false, "The channel or mixer track id." },
            { "group", ValueKind::text, false, "oscillators or effects, or omit." },
            { "slot", ValueKind::integer, false, "Which oscillator or effect slot." },
            { "param", ValueKind::text, true, "The parameter to automate." },
            { "placeClip", ValueKind::flag, false, "Place a clip for it. True if absent." },
            { "startBar", ValueKind::integer, false, "Where the clip begins." },
            { "lengthBars", ValueKind::integer, false, "How many bars it spans." } },
          write });

    all.push_back (
        { "automation_read",
          OpScope::read,
          OpEdits::no,
          "Read one automation curve's points.",
          "Values are 0 to 1 and are mapped onto the parameter's own units when they are "
          "read - a discrete parameter is snapped, because half-on is not a state a "
          "toggle has.\n\n"
          "`stale` is true when the curve points at something that no longer applies - "
          "an effect slot that changed type, an oscillator switched out of wavetable "
          "mode. A stale curve is inert rather than misapplied.",
          { { "id", ValueKind::integer, true, "The automation to read." } },
          readCurve });

    all.push_back (
        { "automation_points_write",
          OpScope::write,
          OpEdits::yes,
          "Add or move points on an automation curve, as one undo step.",
          "A point is a step and a value from 0 to 1. Two points on one step is a curve "
          "with no defined value there, so a point written where one already sits moves "
          "it rather than duplicating it.\n\n"
          "Three shapes are offered and two are stored: 'line' is a curve with no bend, "
          "so switching to 'step' and back returns the curve you had. A stepped segment "
          "ignores its bend rather than losing it.\n\n"
          "`replace` removes whatever was there that you did not write, which is how you "
          "redraw a curve rather than add to it. A curve always keeps at least two points "
          "- one with fewer has no shape to evaluate - so replacing with a single point "
          "leaves one of the old ones behind, and the answer's `points` count says so.",
          { { "id", ValueKind::integer, true, "The automation to shape." },
            { "replace", ValueKind::flag, false, "Clear the existing points first." },
            { "points",
              ValueKind::array,
              true,
              "The points.",
              { { "step", ValueKind::number, true, "Where, in steps from the clip's start." },
                { "value", ValueKind::number, true, "What, from 0 to 1." },
                { "shape", ValueKind::text, false, "line, curve or step. Applies to the RIGHT." },
                { "bend", ValueKind::number, false, "How the curve bends, -1 to 1." } } } },
          writePoints });

    all.push_back ({ "automation_remove",
                     OpScope::write,
                     OpEdits::yes,
                     "Remove automation curves and every clip that played them.",
                     "One undo step. A clip pointing at a removed automation would have nothing to "
                     "drive, so it goes too.",
                     { { "ids",
                         ValueKind::array,
                         true,
                         "The automation ids to remove.",
                         { { "", ValueKind::integer, true, "An automation id." } } } },
                     remove });
}

} // namespace dew::control
