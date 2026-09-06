#include "control/OpsSupport.h"
#include "model/Meter.h"

namespace dew::control
{

namespace
{

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto& entries = arrayArg (args, "entries");

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);
        const auto at = "entries[" + juce::String (i) + "] ";

        for (const auto* field : { "id", "duplicateOf" })
            if (hasArg (entry, field)
                && ! ProjectEdits::findPattern (project, intArg (entry, field)).isValid())
                return ControlResult::failure (at + noSuchPattern (intArg (entry, field)));
    }

    auto* undo = host.undoManager();

    juce::Array<juce::var> touched;

    for (const auto& entry : entries)
    {
        auto pattern = juce::ValueTree {};

        if (hasArg (entry, "id"))
            pattern = ProjectEdits::findPattern (project, intArg (entry, "id"));
        else if (hasArg (entry, "duplicateOf"))
            pattern = ProjectEdits::duplicatePattern (
                project, ProjectEdits::findPattern (project, intArg (entry, "duplicateOf")), undo);
        else
            pattern = ProjectEdits::addPattern (project, undo);

        if (! pattern.isValid())
            continue;

        if (hasArg (entry, "name"))
            ProjectEdits::setProperty (pattern, ids::name, textArg (entry, "name"), undo,
                                       "Rename pattern", true);

        // No length argument, and none to add. A pattern's length is derived
        // from the notes in it - see ProjectEdits::fitPatternToNotes - so the
        // way to make a pattern longer is to write a note further into it, and
        // the way to make it shorter is to remove the notes at the end. A
        // setter here would be a value the next note edit silently overwrote.
        ProjectEdits::fitPatternToNotes (pattern, Meter::of (project).stepsPerBar(), undo);

        touched.add (Obj {}
                         .set ("id", (int) pattern[ids::id])
                         .set ("name", pattern[ids::name].toString())
                         .set ("lengthSteps", (int) pattern[ids::lengthSteps]));
    }

    host.flushEngine();

    return ControlResult::success (
        Obj {}.set ("applied", touched.size()).set ("patterns", arrayOf (touched)));
}

ControlResult read (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto pattern = ProjectEdits::findPattern (project, intArg (args, "id"));

    if (! pattern.isValid())
        return ControlResult::failure (noSuchPattern (intArg (args, "id")));

    const auto onlyChannel = hasArg (args, "channelId") ? intArg (args, "channelId") : -1;

    juce::Array<juce::var> notes;

    for (const auto& note : childrenOfType (pattern, ids::NOTE))
    {
        if (onlyChannel >= 0 && (int) note[ids::ch] != onlyChannel)
            continue;

        notes.add (Obj {}
                       .set ("channelId", (int) note[ids::ch])
                       .set ("step", (int) note[ids::step])
                       .set ("lengthSteps", (int) note[ids::lengthSteps])
                       .set ("pitch", (int) note[ids::pitch])
                       .set ("velocity", (double) note[ids::velocity]));
    }

    return ControlResult::success (Obj {}
                                       .set ("id", (int) pattern[ids::id])
                                       .set ("name", pattern[ids::name].toString())
                                       .set ("lengthSteps", (int) pattern[ids::lengthSteps])
                                       .set ("notes", arrayOf (notes)));
}

ControlResult remove (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    juce::Array<juce::ValueTree> doomed;

    for (const auto& id : arrayArg (args, "ids"))
    {
        const auto pattern = ProjectEdits::findPattern (project, (int) id);

        if (! pattern.isValid())
            return ControlResult::failure (noSuchPattern ((int) id));

        doomed.add (pattern);
    }

    auto removed = 0;

    for (const auto& pattern : doomed)
        if (ProjectEdits::removePattern (project, pattern, host.undoManager()))
            ++removed;

    host.flushEngine();

    return applied (removed, doomed.size() - removed);
}

} // namespace

void appendPatternOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "patterns_write",
          OpScope::write,
          OpEdits::yes,
          "Add, duplicate or rename patterns, as one undo step.",
          "A pattern holds every channel's notes for its span, which is why a section of "
          "an arrangement is one pattern rather than one per instrument.\n\n"
          "`duplicateOf` deep-copies a pattern, notes and all, under a new id - which is "
          "what a repeat that will be varied wants. A repeat that is identical wants one "
          "pattern and two clips instead.\n\n"
          "A pattern's LENGTH is not settable, here or anywhere. It is derived from the "
          "notes the pattern holds, rounded up to a whole bar and never less than one - so "
          "writing a note further in is what makes a pattern longer, and removing the "
          "notes at the end is what makes it shorter. `lengthSteps` is reported back for "
          "reference and is not an argument.",
          { { "entries",
              ValueKind::array,
              true,
              "The patterns to add or change.",
              { { "id", ValueKind::integer, false,
                  "An existing pattern to change. Omit to make a new one." },
                { "duplicateOf", ValueKind::integer, false,
                  "Copy this pattern, notes and all, instead of making an empty one." },
                { "name", ValueKind::text, false, "What the pattern is called." } } } },
          write });

    all.push_back (
        { "patterns_read",
          OpScope::read,
          OpEdits::no,
          "Read one pattern's notes, optionally for one channel only.",
          "The piano roll's contents. Ask for one channel when you only mean one - a "
          "pattern holds every channel's notes, so the whole of a busy one is a lot of "
          "tokens for a question about the bass.",
          { { "id", ValueKind::integer, true, "The pattern to read." },
            { "channelId", ValueKind::integer, false, "Return only this channel's notes." } },
          read });

    all.push_back (
        { "patterns_remove",
          OpScope::write,
          OpEdits::yes,
          "Remove patterns and every playlist clip that played them.",
          "One undo step. A clip pointing at a missing pattern would be dropped by the "
          "next engine rebuild anyway, so removing it here keeps the document "
          "consistent rather than merely tidy.\n\n"
          "Refuses the last pattern: a project with none has nothing to edit and nothing "
          "to play. A refusal is counted as skipped rather than failing the batch.",
          { { "ids",
              ValueKind::array,
              true,
              "The pattern ids to remove.",
              { { "", ValueKind::integer, true, "A pattern id." } } } },
          remove });
}

} // namespace dew::control
