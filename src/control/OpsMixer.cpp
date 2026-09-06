#include "control/OpsSupport.h"
#include "model/ProjectSchema.h"

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

        if (hasArg (entry, "id")
            && ! ProjectEdits::findMixerTrack (project, intArg (entry, "id")).isValid())
            return ControlResult::failure ("entries[" + juce::String (i) + "] "
                                           + noSuchMixerTrack (intArg (entry, "id")));
    }

    const auto adding = [&entries]
    {
        auto count = 0;

        for (const auto& entry : entries)
            if (! hasArg (entry, "id"))
                ++count;

        return count;
    }();

    // Refused before anything is written rather than discovered halfway. An
    // insert past the cap is a fader that moves nothing, which is the same
    // silent wrongness the effect chain's cap exists to prevent.
    if (ProjectEdits::countMixerTracks (project) + adding > kMaxMixerTracks)
        return ControlResult::failure ("that would take the mixer past "
                                       + juce::String (kMaxMixerTracks)
                                       + " inserts, which is as many as the engine renders.");

    auto* undo = host.undoManager();

    juce::Array<juce::var> touched;

    for (const auto& entry : entries)
    {
        auto track = hasArg (entry, "id")
                         ? ProjectEdits::findMixerTrack (project, intArg (entry, "id"))
                         : ProjectEdits::addMixerTrack (project, textArg (entry, "name", "Insert"),
                                                        undo);

        if (! track.isValid())
            continue;

        if (hasArg (entry, "name"))
            ProjectEdits::setProperty (track, ids::name, textArg (entry, "name"), undo,
                                       TransactionName { "Rename insert" }, true);

        if (hasArg (entry, "colour"))
            ProjectEdits::setColour (track, textArg (entry, "colour"), undo, true);

        touched.add (
            Obj {}.set ("id", (int) track[ids::id]).set ("name", track[ids::name].toString()));
    }

    host.flushEngine();

    return ControlResult::success (
        Obj {}.set ("applied", touched.size()).set ("mixerTracks", arrayOf (touched)));
}

ControlResult remove (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    juce::Array<juce::ValueTree> doomed;

    for (const auto& id : arrayArg (args, "ids"))
    {
        const auto track = ProjectEdits::findMixerTrack (project, (int) id);

        if (! track.isValid())
            return ControlResult::failure (noSuchMixerTrack ((int) id));

        doomed.add (track);
    }

    auto removed = 0;

    for (const auto& track : doomed)
        if (ProjectEdits::removeMixerTrack (project, track, host.undoManager()))
            ++removed;

    host.flushEngine();

    return applied (removed, doomed.size() - removed);
}

} // namespace

void appendMixerOps (std::vector<OpSpec>& all)
{
    all.push_back (
        { "mixer_write",
          OpScope::write,
          OpEdits::yes,
          "Add mixer inserts, or rename and recolour existing ones.",
          "An upsert, like channels_write: an entry with an `id` changes that insert, "
          "one without adds a new one.\n\n"
          "A fader and a pan are parameters, not fields here - reach them with "
          "params_write, target 'mixerTrack'. The master is addressed as target "
          "'master' and has a fader and nothing else.",
          { { "entries",
              ValueKind::array,
              true,
              "The inserts to add or change.",
              { { "id", ValueKind::integer, false,
                  "An existing insert to change. Omit to add one." },
                { "name", ValueKind::text, false, "What the insert is called." },
                { "colour", ValueKind::text, false,
                  "A hex colour. Empty means inherit the colours routed into it." } } } },
          write });

    all.push_back (
        { "mixer_remove",
          OpScope::write,
          OpEdits::yes,
          "Remove mixer inserts, their effects and the routing they leave behind.",
          "Channels routed into a removed insert are re-pointed at the first remaining "
          "one in the same undo step, because a channel that has lost its insert is "
          "still a channel with notes in it - unlike a clip that has lost its pattern.\n\n"
          "Refuses the last insert and refuses the master. Skipped entries are counted "
          "in the answer rather than reported as failures.",
          { { "ids",
              ValueKind::array,
              true,
              "The insert ids to remove.",
              { { "", ValueKind::integer, true, "A mixer insert id." } } } },
          remove });
}

} // namespace dew::control
