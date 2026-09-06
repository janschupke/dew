#include "control/OpsSupport.h"
#include "model/AutomationTargets.h"
#include "model/ProjectSchema.h"

namespace dew::control
{

namespace
{

/** A summary small enough to read on every turn.

    project_read hands back the whole document, which for a real arrangement is
    tens of thousands of tokens of notes - correct, and the wrong thing to open
    with. This is the orientation: the counts and the names, and no note, point
    or clip anywhere in it. A caller that needs the notes of one pattern asks
    for that pattern.

    The distinction is the same one the interface makes between the channel rack
    and the piano roll, and it is the difference between a client that can hold
    a conversation about a song and one that spends its whole context on it.
*/
ControlResult describe (ControlHost& host, const juce::var&)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    juce::Array<juce::var> channels;

    for (const auto& channel : childrenOfType (project, ids::CHANNEL))
        channels.add (Obj {}
                          .set ("id", (int) channel[ids::id])
                          .set ("name", channel[ids::name].toString())
                          .set ("source", channel[ids::source].toString())
                          .set ("mixerTrackId", (int) channel[ids::mixerTrackId])
                          .set (ids::muted, (bool) channel[ids::muted])
                          .set ("effects", ProjectEdits::countEffects (channel)));

    juce::Array<juce::var> patterns;

    for (const auto& pattern : childrenOfType (project, ids::PATTERN))
        patterns.add (Obj {}
                          .set ("id", (int) pattern[ids::id])
                          .set ("name", pattern[ids::name].toString())
                          .set ("lengthSteps", (int) pattern[ids::lengthSteps])
                          .set ("notes", childrenOfType (pattern, ids::NOTE).size()));

    juce::Array<juce::var> tracks;
    const auto lanes = playlistTracks (project);

    for (int i = 0; i < lanes.size(); ++i)
        tracks.add (Obj {}
                        .set ("index", i)
                        .set ("name", lanes[i][ids::name].toString())
                        .set ("clips", childrenOfType (lanes[i], ids::CLIP).size()));

    juce::Array<juce::var> inserts;
    const auto mixer = project.getChildWithName (ids::MIXER);

    for (const auto& track : childrenOfType (mixer, ids::MIXER_TRACK))
        inserts.add (Obj {}
                         .set ("id", (int) track[ids::id])
                         .set ("name", track[ids::name].toString())
                         .set ("effects", ProjectEdits::countEffects (track)));

    juce::Array<juce::var> automations;

    for (const auto& automation : childrenOfType (project, ids::AUTOMATION))
        automations.add (Obj {}
                             .set ("id", (int) automation[ids::id])
                             .set ("name", automation[ids::name].toString())
                             .set ("scope", automation[ids::scope].toString())
                             .set ("targetId", (int) automation[ids::targetId])
                             .set ("slot", (int) automation[ids::slot])
                             .set ("param", automation[ids::param].toString())
                             .set ("points", childrenOfType (automation, ids::POINT).size()));

    return ControlResult::success (
        Obj {}
            .set ("name", project[ids::name].toString())
            .set ("file", host.projectFile().getFullPathName())
            .set ("modified", host.isProjectModified())
            .set ("formatVersion", (int) project[ids::formatVersion])
            .set (ids::tempoBpm, (double) project[ids::tempoBpm])
            .set ("stepsPerBeat", (int) project[ids::stepsPerBeat])
            .set ("beatsPerBar", (int) project[ids::beatsPerBar])
            .set ("beatUnit", (int) project[ids::beatUnit])
            .set ("barsInSong", (int) project[ids::barsInSong])
            .set ("channels", arrayOf (channels))
            .set ("patterns", arrayOf (patterns))
            .set ("playlistTracks", arrayOf (tracks))
            .set ("mixerTracks", arrayOf (inserts))
            .set ("automations", arrayOf (automations))
            .set ("hasScore", ProjectEdits::scoreSource (project).isNotEmpty()));
}

/** The document itself, through the schema that reads and writes it.

    varFromTree rather than a hand-written walk, so what this returns is exactly
    what a .dew file holds - the same table drives reading, writing, validation
    and now this. A second walk would be a fourth place free to disagree about
    what a project is.
*/
ControlResult read (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto whole = varFromTree (project, projectSpec());

    if (! hasArg (args, "member"))
        return ControlResult::success (whole);

    const auto member = textArg (args, "member");
    const auto value = whole[juce::Identifier (member)];

    if (value.isVoid() || value.isUndefined())
        return ControlResult::failure ("the project has no member called '" + member + "'.");

    return ControlResult::success (value);
}

/** New, open, save. Undo and redo are here too, because they are the same kind
    of thing: a verb about the document rather than an edit to it.

    Every one of them can refuse, and says so. A host with unsaved changes it
    cannot ask the user about must refuse `new` and `open` - discarding somebody
    else's work because a client asked is not a thing a grant covers.
*/
ControlResult command (ControlHost& host, const juce::var& args)
{
    const auto verb = textArg (args, "verb");
    const auto path = textArg (args, "path");

    const auto refused = [&verb] (const char* why)
    { return ControlResult::failure (juce::String ("'") + verb + "' was refused: " + why); };

    if (verb == "new")
        return host.newProject() ? ControlResult::success (Obj {}.set ("done", true))
                                 : refused ("the host would not replace the open project.");

    if (verb == "open")
    {
        if (path.isEmpty())
            return ControlResult::failure ("'open' needs a path.");

        const juce::File file { path };

        if (! file.existsAsFile())
            return ControlResult::failure ("there is no file at " + path + ".");

        return host.openProject (file) ? ControlResult::success (Obj {}.set ("done", true))
                                       : refused ("the host would not open it.");
    }

    if (verb == "save")
    {
        const auto saved = path.isEmpty() ? host.saveProject() : host.saveProjectAs ({ path });

        return saved ? ControlResult::success (
                           Obj {}.set ("file", host.projectFile().getFullPathName()))
                     : refused ("the host would not save.");
    }

    auto* undo = host.undoManager();

    if (undo == nullptr)
        return refused ("there is no undo history.");

    if (verb == "undo")
        return ControlResult::success (Obj {}.set ("done", undo->undo()));

    if (verb == "redo")
        return ControlResult::success (Obj {}.set ("done", undo->redo()));

    return ControlResult::failure ("'" + verb
                                   + "' is not a verb. Use new, open, save, undo or redo.");
}

/** The song's own numbers.

    setMeter rather than two property writes, because a bar is a unit the whole
    arrangement is measured in: a clip is stored in BARS, so redefining one moves
    every clip, and ProjectEdits rescales them in the same transaction. Writing
    beatsPerBar by hand would leave a one-bar clip of a sixteen-step pattern
    spanning twelve steps in 3/4, with the pattern's last four steps silently
    not sounding.
*/
ControlResult writeStructure (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    auto* undo = host.undoManager();

    auto changed = 0;

    if (hasArg (args, "name"))
    {
        ProjectEdits::setProperty (project, ids::name, textArg (args, "name"), undo,
                                   TransactionName { "Rename project" }, true);
        ++changed;
    }

    if (hasArg (args, ids::tempoBpm))
    {
        ProjectEdits::setProperty (project, ids::tempoBpm, numberArg (args, ids::tempoBpm), undo,
                                   TransactionName { "Set tempo" }, true);
        ++changed;
    }

    if (hasArg (args, "barsInSong"))
    {
        ProjectEdits::setProperty (project, ids::barsInSong, intArg (args, "barsInSong"), undo,
                                   TransactionName { "Set song length" }, true);
        ++changed;
    }

    auto exact = true;

    if (hasArg (args, "beatsPerBar") || hasArg (args, "beatUnit"))
    {
        const auto beats = intArg (args, "beatsPerBar", (int) project[ids::beatsPerBar]);
        const auto unit = intArg (args, "beatUnit", (int) project[ids::beatUnit]);

        ProjectEdits::setMeter (project, beats, unit, undo, &exact);
        ++changed;
    }

    host.flushEngine();

    return ControlResult::success (
        Obj {}.set ("applied", changed).set ("clipsLandedOnWholeBars", exact));
}

} // namespace

void appendProjectOps (std::vector<OpSpec>& all)
{
    all.push_back ({ "project_describe",
                     OpScope::read,
                     OpEdits::no,
                     "Summarise the open project: tempo, meter, and every channel, pattern, "
                     "playlist track, mixer insert and automation by name and id.",
                     "Start here. It is the cheap read - counts and names, and not one note, "
                     "clip or automation point - so it costs a few hundred tokens whatever the "
                     "size of the arrangement.\n\n"
                     "Use the ids it returns to address everything else. Reach for project_read "
                     "only when you need the contents of something this named.",
                     {},
                     describe });

    all.push_back ({ "project_read",
                     OpScope::read,
                     OpEdits::no,
                     "Read the whole project document, or one top-level member of it, exactly as a "
                     ".dew file stores it.",
                     "The document through the same schema that reads and writes the file, so "
                     "what you get back is what is on disk rather than a summary of it.\n\n"
                     "This is large: a real arrangement is tens of thousands of tokens of notes. "
                     "Pass `member` to take one part - 'channels', 'patterns', 'playlist', "
                     "'mixer' - and prefer project_describe when you only need to know what "
                     "exists.",
                     { { "member", ValueKind::text, false,
                         "One top-level member to return instead of the whole document." } },
                     read });

    all.push_back ({ "project_command",
                     OpScope::write,
                     OpEdits::no,
                     "Create, open or save the project, or step its undo history.",
                     "Verbs about the document rather than edits to it. 'new' and 'open' replace "
                     "what is open and may be refused when there are unsaved changes; 'save' "
                     "writes to the project's own file, or to `path` if you give one.\n\n"
                     "'undo' and 'redo' step the same history the user's own Cmd-Z steps. Every "
                     "operation in this table is exactly one step of it, however many entries its "
                     "batch carried.",
                     { { "verb", ValueKind::text, true, "new, open, save, undo or redo." },
                       { "path", ValueKind::text, false,
                         "The file to open, or to save to. Required by 'open'." } },
                     command });

    all.push_back (
        { "structure_write",
          OpScope::write,
          OpEdits::yes,
          "Set the project's name, tempo, metre or length in bars.",
          "The metre is not a tempo. Changing beatsPerBar redefines what a bar IS, and "
          "a clip is stored in bars - so every clip's start and length is rescaled in "
          "the same undo step to hold its position in time. The answer says whether "
          "every clip landed on a whole bar; when it did not, some were rounded.\n\n"
          "beatUnit is notational: it names the metre and labels the snap divisions, "
          "and does not change how long anything sounds for.",
          { { "name", ValueKind::text, false, "The project's title." },
            { ids::tempoBpm.toString(), ValueKind::number, false, "Beats per minute." },
            { "beatsPerBar", ValueKind::integer, false,
              "The top of the time signature. Load-bearing: it is the bar." },
            { "beatUnit", ValueKind::integer, false,
              "The bottom of the time signature. Notational only." },
            { "barsInSong", ValueKind::integer, false, "How long the arrangement is." } },
          writeStructure });
}

} // namespace dew::control
