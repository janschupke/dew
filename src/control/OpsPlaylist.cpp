#include "control/OpsSupport.h"
#include "model/Meter.h"

namespace dew::control
{

namespace
{

/** One entry of playlist_tracks_write.

    Built rather than written as a brace list, because `mute` is named by the
    identifier it writes - see channelFields in OpsChannels.cpp for the
    argument.

    There was a `solo` beside it, and its description said what solo is: silence
    every OTHER lane. That is a write to every lane in the project dressed as a
    write to one, and it is why a lane's audibility could not be read off the
    lane. A lane now has one state, and an agent silences the others by saying
    so.
*/
std::vector<ArgSpec> laneFields()
{
    return { { "index", ValueKind::integer, false, "An existing lane. Omit to append a new one." },
             { "name", ValueKind::text, false, "What the lane is called." },
             { ids::mute.toString(), ValueKind::flag, false, "Silence everything on the lane." },
             { "colour", ValueKind::text, false, "A hex colour, or empty to inherit." } };
}

ControlResult writeTracks (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto& entries = arrayArg (args, "entries");

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);

        if (hasArg (entry, "index")
            && ! playlistTrackAt (project, intArg (entry, "index")).isValid())
            return ControlResult::failure ("entries[" + juce::String (i) + "] "
                                           + noSuchTrack (intArg (entry, "index")));
    }

    auto* undo = host.undoManager();

    juce::Array<juce::var> touched;

    for (const auto& entry : entries)
    {
        // A playlist track carries NO id - tracks are positional, unlike
        // channels and mixer inserts - so an index is the whole address, and a
        // track added by an earlier entry of this same batch is addressable by
        // the index it lands on.
        auto track = hasArg (entry, "index") ? playlistTrackAt (project, intArg (entry, "index"))
                                             : ProjectEdits::addPlaylistTrack (
                                                   project, textArg (entry, "name", "Track"), undo);

        if (! track.isValid())
            continue;

        if (hasArg (entry, "name"))
            ProjectEdits::setProperty (track, ids::name, textArg (entry, "name"), undo,
                                       "Rename track", true);

        if (hasArg (entry, ids::mute))
            ProjectEdits::setProperty (track, ids::mute, flagArg (entry, ids::mute), undo,
                                       "Mute track", true);

        if (hasArg (entry, "colour"))
            ProjectEdits::setColour (track, textArg (entry, "colour"), undo, true);

        touched.add (Obj {}
                         .set ("index", playlistTracks (project).indexOf (track))
                         .set ("name", track[ids::name].toString()));
    }

    host.flushEngine();

    return ControlResult::success (
        Obj {}.set ("applied", touched.size()).set ("tracks", arrayOf (touched)));
}

ControlResult removeTracks (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    juce::Array<juce::ValueTree> doomed;

    for (const auto& index : arrayArg (args, "indexes"))
    {
        const auto track = playlistTrackAt (project, (int) index);

        if (! track.isValid())
            return ControlResult::failure (noSuchTrack ((int) index));

        doomed.add (track);
    }

    for (const auto& track : doomed)
        ProjectEdits::removePlaylistTrack (project, track, host.undoManager());

    host.flushEngine();

    return applied (doomed.size());
}

ControlResult writeClips (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto& entries = arrayArg (args, "clips");

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries.getReference (i);
        const auto at = "clips[" + juce::String (i) + "] ";
        const auto kind = textArg (entry, "kind", "pattern");

        if (! playlistTrackAt (project, intArg (entry, "track")).isValid())
            return ControlResult::failure (at + noSuchTrack (intArg (entry, "track")));

        if (intArg (entry, "startBar") < 0 || intArg (entry, "startStep") < 0)
            return ControlResult::failure (at + "a clip cannot start before the beginning.");

        if (kind == "pattern")
        {
            if (! ProjectEdits::findPattern (project, intArg (entry, "patternId")).isValid())
                return ControlResult::failure (at + noSuchPattern (intArg (entry, "patternId")));
        }
        else if (kind == "audio")
        {
            const auto channel = ProjectEdits::findChannel (project, intArg (entry, "channelId"));

            if (! channel.isValid())
                return ControlResult::failure (at + noSuchChannel (intArg (entry, "channelId")));

            if (! ProjectEdits::playsClips (channel))
                return ControlResult::failure (at + "channel "
                                               + juce::String (intArg (entry, "channelId"))
                                               + " is not played by clips.");
        }
        else if (kind == "automation")
        {
            if (! ProjectEdits::findAutomation (project, intArg (entry, "automationId")).isValid())
                return ControlResult::failure (at + "no automation with id "
                                               + juce::String (intArg (entry, "automationId"))
                                               + ".");
        }
        else
        {
            return ControlResult::failure (at + "'" + kind
                                           + "' is not a kind of clip. Use pattern, audio or "
                                             "automation.");
        }
    }

    auto* undo = host.undoManager();

    auto placed = 0;

    for (const auto& entry : entries)
    {
        auto track = playlistTrackAt (project, intArg (entry, "track"));
        const auto kind = textArg (entry, "kind", "pattern");
        // BARS by default, because arranging is thinking in bars and every
        // caller of this op was written that way. `startStep` and `lengthSteps`
        // are the way to say something a bar cannot - a fill that begins on the
        // last beat - and they win where they are given, so nothing that
        // already worked has to change.
        const auto perBar = juce::jmax (1, Meter::of (project).stepsPerBar());

        const auto startStep = hasArg (entry, "startStep") ? intArg (entry, "startStep")
                                                           : intArg (entry, "startBar") * perBar;

        const auto lengthSteps = hasArg (entry, "lengthSteps")
                                     ? juce::jmax (1, intArg (entry, "lengthSteps"))
                                     : juce::jmax (1, intArg (entry, "lengthBars", 1)) * perBar;

        auto clip = juce::ValueTree {};

        if (kind == "pattern")
            clip = ProjectEdits::addClip (track, intArg (entry, "patternId"), startStep,
                                          lengthSteps, undo);
        else if (kind == "audio")
            clip = ProjectEdits::addAudioClip (track, intArg (entry, "channelId"), startStep,
                                               lengthSteps, undo);
        else
            clip = ProjectEdits::addAutomationClip (track, intArg (entry, "automationId"),
                                                    startStep, lengthSteps, undo);

        if (clip.isValid())
            ++placed;
    }

    // Trailing empty bars are a deliberate silence, so this grows and never
    // shrinks - the same rule a pattern's length follows.
    const auto grew = ProjectEdits::growSongToFitClips (project, undo);

    host.flushEngine();

    return ControlResult::success (Obj {}
                                       .set ("applied", placed)
                                       .set ("songGrew", grew)
                                       .set ("barsInSong", (int) project[ids::barsInSong]));
}

ControlResult removeClips (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    struct Doomed
    {
        juce::ValueTree track;
        juce::ValueTree clip;
    };

    std::vector<Doomed> doomed;

    for (const auto& entry : arrayArg (args, "clips"))
    {
        const auto track = playlistTrackAt (project, intArg (entry, "track"));

        if (! track.isValid())
            return ControlResult::failure (noSuchTrack (intArg (entry, "track")));

        const auto perBar = juce::jmax (1, Meter::of (project).stepsPerBar());

        const auto atStep = hasArg (entry, "atStep") ? intArg (entry, "atStep")
                                                     : intArg (entry, "atBar") * perBar;

        const auto clip = ProjectEdits::findClipAtStep (track, atStep);

        if (clip.isValid())
            doomed.push_back ({ track, clip });
    }

    for (const auto& entry : doomed)
        ProjectEdits::removeClip (entry.track, entry.clip, host.undoManager());

    host.flushEngine();

    return applied ((int) doomed.size());
}

} // namespace

void appendPlaylistOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "playlist_tracks_write",
          OpScope::write,
          OpEdits::yes,
          "Add arrangement lanes, or rename, mute, solo and recolour existing ones.",
          "A lane carries no id: lanes are positional, unlike channels and mixer "
          "inserts, so an index is the whole address. An entry with an `index` changes "
          "that lane and one without appends a new one.\n\n"
          "A lane is not a channel. Any lane can carry a clip of any pattern, and a "
          "pattern holds every channel's notes - so lanes are sections of the song, not "
          "instruments.",
          { { "entries", ValueKind::array, true, "The lanes to add or change.", laneFields() } },
          writeTracks });

    all.push_back (
        { "playlist_tracks_remove",
          OpScope::write,
          OpEdits::yes,
          "Remove arrangement lanes and the clips on them.",
          "One undo step. The clips go with the lane. Automations do NOT - an automation "
          "is a reusable definition that can be placed again, unlike the notes a removed "
          "channel leaves behind, which nothing can reach.\n\n"
          "Removing a lane renumbers the ones after it, so give every index you mean in "
          "one call rather than calling repeatedly.",
          { { "indexes",
              ValueKind::array,
              true,
              "The lane indexes to remove.",
              { { "", ValueKind::integer, true, "A lane index." } } } },
          removeTracks });

    all.push_back (
        { "clips_write",
          OpScope::write,
          OpEdits::yes,
          "Place clips on the arrangement: patterns, audio takes or automation curves.",
          "A clip is measured in BARS, and a bar is beatsPerBar steps - so a clip's "
          "length in time follows the metre. Placing two clips of one pattern is how a "
          "section repeats identically; duplicating the pattern first is how it repeats "
          "with variation.\n\n"
          "The song grows to fit what you place, and never shrinks: trailing empty bars "
          "are a deliberate silence.\n\n"
          "Refuses the whole batch on a bad lane, pattern, channel or automation rather "
          "than placing some of it.",
          { { "clips",
              ValueKind::array,
              true,
              "The clips to place.",
              { { "track", ValueKind::integer, true, "Which lane, counting from 0." },
                { "kind", ValueKind::text, false,
                  "pattern, audio or automation. Pattern if absent." },
                { "startBar", ValueKind::integer, false, "Where it begins, counting from 0." },
                { "lengthBars", ValueKind::integer, false, "How many bars it spans." },
                { "startStep", ValueKind::integer, false,
                  "Where it begins, in steps. Overrides startBar, and is the only way to "
                  "start a clip off a bar line." },
                { "lengthSteps", ValueKind::integer, false,
                  "How many steps it spans. Overrides lengthBars." },
                { "patternId", ValueKind::integer, false, "For a pattern clip." },
                { "channelId", ValueKind::integer, false, "For an audio clip." },
                { "automationId", ValueKind::integer, false, "For an automation clip." } } } },
          writeClips });

    all.push_back ({ "clips_remove",
                     OpScope::write,
                     OpEdits::yes,
                     "Remove the clip covering a bar on a lane.",
                     "Addressed by where it is rather than by an id, because that is how a clip is "
                     "identified on screen: a lane and a bar. A bar with no clip on it is skipped "
                     "rather than failing the batch, so clearing a range is safe to ask for twice.",
                     { { "clips",
                         ValueKind::array,
                         true,
                         "Lane-and-bar pairs.",
                         { { "track", ValueKind::integer, true, "Which lane." },
                           { "atBar", ValueKind::integer, false, "Any bar the clip covers." },
                           { "atStep", ValueKind::integer, false,
                             "Any step the clip covers. Overrides atBar, and is how to reach "
                             "a clip that does not begin on a bar line." } } } },
                     removeClips });
}

} // namespace dew::control
