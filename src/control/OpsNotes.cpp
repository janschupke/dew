#include <climits>

#include "control/OpsSupport.h"
#include "model/Meter.h"
#include "model/NoteTools.h"

namespace dew::control
{

namespace
{

// The vocabulary, spelled once. Everything else names these.
const juce::String kQuantize { "quantize" };
const juce::String kTranspose { "transpose" };

} // namespace

juce::StringArray noteTransformVerbs()
{
    return { kQuantize, kTranspose };
}

namespace
{

/** The pattern an operation names, or the invalid tree plus the sentence. */
juce::ValueTree patternFor (const juce::ValueTree& project, const juce::var& args)
{
    return ProjectEdits::findPattern (project, intArg (args, "patternId"));
}

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    auto pattern = patternFor (project, args);

    if (! pattern.isValid())
        return ControlResult::failure (noSuchPattern (intArg (args, "patternId")));

    const auto& entries = arrayArg (args, "notes");

    // Checked before written, so a batch of two hundred notes with one bad
    // channel id leaves the pattern exactly as it was.
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);
        const auto at = "notes[" + juce::String (i) + "] ";
        const auto channelId = intArg (entry, "channelId");
        const auto channel = ProjectEdits::findChannel (project, channelId);

        if (! channel.isValid())
            return ControlResult::failure (at + noSuchChannel (channelId));

        // An audio channel is played by clips on the playlist and takes no
        // notes at all. Writing them would put notes in the file that nothing
        // ever sounds, which is worse than a refusal because nothing says so.
        if (! ProjectEdits::playsNotes (channel))
            return ControlResult::failure (at + "channel " + juce::String (channelId) + " is a "
                                           + channel[ids::source].toString()
                                           + " channel, which is played by clips rather than by "
                                             "notes.");

        const auto pitch = intArg (entry, "pitch");

        if (pitch < NoteTools::lowestPitch || pitch > NoteTools::highestPitch)
            return ControlResult::failure (at + "pitch " + juce::String (pitch) + " is outside "
                                           + juce::String (NoteTools::lowestPitch) + ".."
                                           + juce::String (NoteTools::highestPitch)
                                           + ", which is what the editor can show.");

        if (intArg (entry, "step") < 0)
            return ControlResult::failure (at + "step cannot be negative.");
    }

    auto* undo = host.undoManager();

    auto added = 0;
    auto changed = 0;

    for (const auto& entry : entries)
    {
        const auto channelId = intArg (entry, "channelId");
        const auto step = intArg (entry, "step");
        const auto pitch = intArg (entry, "pitch");
        const auto length = juce::jmax (1, intArg (entry, "lengthSteps", 1));
        const auto velocity = numberArg (entry, "velocity", 0.8);

        // An upsert on the three fields that IDENTIFY a note - channel, step
        // and pitch. Writing the same note twice is then idempotent, which is
        // what lets a caller re-send a bar it has already sent without
        // doubling every note in it, and two notes on one channel at one step
        // and pitch retrigger one voice at one instant anyway: the second is
        // inaudible and invisible and only grows the file.
        if (auto existing = ProjectEdits::findNote (pattern, channelId, step, pitch);
            existing.isValid())
        {
            ProjectEdits::resizeNote (existing, length, undo);
            ProjectEdits::setNoteVelocity (existing, velocity, undo);
            ++changed;
        }
        else
        {
            ProjectEdits::addNote (pattern, channelId, step, length, pitch, (float) velocity, undo);
            ++added;
        }
    }

    // Both ways: a pattern's length is derived from the notes in it, in whole
    // bars, so writing past the end lengthens it and clearing the last bar
    // shortens it. See ProjectEdits::fitPatternToNotes.
    const auto refitted = ProjectEdits::fitPatternToNotes (pattern,
                                                           Meter::of (project).stepsPerBar(), undo);

    host.flushEngine();

    return ControlResult::success (Obj {}
                                       .set ("added", added)
                                       .set ("changed", changed)
                                       .set ("patternRefitted", refitted)
                                       .set ("lengthSteps", (int) pattern[ids::lengthSteps]));
}

ControlResult remove (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    auto pattern = patternFor (project, args);

    if (! pattern.isValid())
        return ControlResult::failure (noSuchPattern (intArg (args, "patternId")));

    const auto onlyChannel = hasArg (args, "channelId") ? intArg (args, "channelId") : -1;
    const auto fromStep = hasArg (args, "fromStep") ? intArg (args, "fromStep") : 0;
    const auto toStep = hasArg (args, "toStep") ? intArg (args, "toStep") : INT_MAX;

    const auto& listed = arrayArg (args, "notes");

    // Resolved to NODES first. Removing while walking the children mutates the
    // list being walked, which is the shape that silently skips every other
    // note - and a step range that is resolved after the first removal is a
    // range over a different pattern than the one the caller described.
    juce::Array<juce::ValueTree> doomed;

    if (listed.isEmpty())
    {
        for (const auto& note : childrenOfType (pattern, ids::NOTE))
        {
            const auto step = (int) note[ids::step];

            if (onlyChannel >= 0 && (int) note[ids::ch] != onlyChannel)
                continue;

            if (step < fromStep || step > toStep)
                continue;

            doomed.add (note);
        }
    }
    else
    {
        for (const auto& entry : listed)
        {
            const auto note = ProjectEdits::findNote (pattern, intArg (entry, "channelId"),
                                                      intArg (entry, "step"),
                                                      intArg (entry, "pitch"));

            if (note.isValid())
                doomed.add (note);
        }
    }

    for (const auto& note : doomed)
        ProjectEdits::removeNote (pattern, note, host.undoManager());

    ProjectEdits::fitPatternToNotes (pattern, Meter::of (project).stepsPerBar(),
                                     host.undoManager());

    host.flushEngine();

    return applied (doomed.size());
}

ControlResult transform (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    auto pattern = patternFor (project, args);

    if (! pattern.isValid())
        return ControlResult::failure (noSuchPattern (intArg (args, "patternId")));

    const auto channelId = intArg (args, "channelId");
    const auto verb = textArg (args, "verb");

    // scopeFor is the one scope rule every caller shares. An empty selection
    // means "every note on the channel", which is what it means in the editor
    // too - so this operation applies to the same notes the same menu item
    // would.
    const auto scope = NoteTools::scopeFor (pattern, channelId, {});

    if (scope.isEmpty())
        return applied (0);

    auto* undo = host.undoManager();

    if (verb == kQuantize)
    {
        const auto stepsPerBeat = (int) project[ids::stepsPerBeat];
        const auto beatsPerBar = (int) project[ids::beatsPerBar];
        const auto snap = NoteTools::snapFromIndex (intArg (args, "snap"));
        const auto snapSteps = NoteTools::stepsForSnap (snap, stepsPerBeat, beatsPerBar);

        const auto collapsed = NoteTools::quantize (pattern, scope, snapSteps,
                                                    Meter::of (project).stepsPerBar(), undo);
        host.flushEngine();

        return ControlResult::success (
            Obj {}
                .set ("applied", scope.size())
                .set ("collapsedDuplicates", collapsed)
                .set ("snap", NoteTools::nameForSnap (snap, (int) project[ids::beatUnit])));
    }

    if (verb == kTranspose)
    {
        // Clamped as a GROUP, so a chord against the top of the range keeps its
        // intervals instead of compressing into itself.
        const auto moved = NoteTools::transpose (scope, intArg (args, "semitones"),
                                                 NoteTools::lowestPitch, NoteTools::highestPitch,
                                                 undo);
        host.flushEngine();

        return ControlResult::success (
            Obj {}.set ("applied", scope.size()).set ("semitonesApplied", moved));
    }

    return ControlResult::failure ("'" + verb + "' is not a transform. Use "
                                   + noteTransformVerbs().joinIntoString (" or ") + ".");
}

std::vector<ArgSpec> noteFields()
{
    return { { "channelId", ValueKind::integer, true, "Which channel sounds the note." },
             { "step", ValueKind::integer, true, "When it starts, in steps from the pattern's 0." },
             { "pitch", ValueKind::integer, true, "MIDI note number. 60 is middle C." },
             { "lengthSteps", ValueKind::integer, false, "How long it lasts. One step if absent." },
             { "velocity", ValueKind::number, false, "How hard it is struck, 0.05 to 1." } };
}

} // namespace

void appendNoteOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "notes_write",
          OpScope::write,
          OpEdits::yes,
          "Write notes into a pattern, as one undo step.",
          "An upsert on the three fields that identify a note - channel, step and pitch "
          "- so sending the same bar twice does not double it.\n\n"
          "Time is whole steps. A step is 1/stepsPerBeat of a beat, which "
          "project_describe reports; at the default 4 a step is a sixteenth note. There "
          "is no fractional step and no tuplet that the grid does not divide.\n\n"
          "The pattern grows to fit what you write, and never shrinks - a pattern longer "
          "than its notes is a deliberate rest at the end. Refuses the whole batch on a "
          "bad channel or an out-of-range pitch rather than writing half of it.\n\n"
          "For anything longer than a few bars, prefer score_write and score_compile: "
          "the language says what the music IS, and this says where every note goes.",
          { { "patternId", ValueKind::integer, true, "The pattern to write into." },
            { "notes", ValueKind::array, true, "The notes.", noteFields() } },
          write });

    all.push_back (
        { "notes_remove",
          OpScope::write,
          OpEdits::yes,
          "Remove notes from a pattern, by list or by range.",
          "Give `notes` to remove exactly those. Give none and it removes every note in "
          "the channel and step range you describe - with no channel and no range, that "
          "is every note in the pattern.\n\n"
          "One undo step either way.",
          { { "patternId", ValueKind::integer, true, "The pattern to remove from." },
            { "notes", ValueKind::array, false, "Exactly these notes.", noteFields() },
            { "channelId", ValueKind::integer, false, "Limit a range removal to one channel." },
            { "fromStep", ValueKind::integer, false, "First step of the range, inclusive." },
            { "toStep", ValueKind::integer, false, "Last step of the range, inclusive." } },
          remove });

    all.push_back (
        { "notes_transform",
          OpScope::write,
          OpEdits::yes,
          "Quantize or transpose every note on one channel of a pattern.",
          "Quantizing rounds each note's START to the grid and deliberately leaves "
          "lengths alone: quantizing length as well turns an eighth-note line into a "
          "legato blur. Two notes that land on the same step and pitch are collapsed "
          "into one, keeping the longer - they retrigger one voice at one instant, so "
          "the second was never audible.\n\n"
          "Transposing clamps the notes as a GROUP, so a chord against the top of the "
          "range keeps its intervals rather than compressing. The answer says how many "
          "semitones were actually applied, which is zero when the group is already "
          "against a limit.",
          { { "patternId", ValueKind::integer, true, "The pattern to transform." },
            { "channelId", ValueKind::integer, true, "Which channel's notes." },
            { "verb", ValueKind::text, true, "quantize or transpose." },
            { "snap", ValueKind::integer, false,
              "For quantize: 0 sixteenth, 1 eighth, 2 quarter, 3 half, 4 bar." },
            { "semitones", ValueKind::integer, false, "For transpose: how far, and which way." } },
          transform });
}

} // namespace dew::control
