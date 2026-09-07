// The arrangement half of the demo vocabulary: the project, its mixer and its
// lanes. See DemoBuilders.h for why the vocabulary is split at all.

#include "model/DemoBuilders.h"

#include "i18n/Strings.h"
#include "model/DemoSpecs.h"
#include "model/EntityColour.h"
#include "model/ProjectSchema.h"
#include "model/TreeWalk.h"

namespace dew::demo
{

namespace
{

/** "Channel 3", from the catalogue, in the REFERENCE locale.

    A demo is compared against a committed file, so its content names have to
    be the same bytes on every machine whatever language dew is running in -
    which is what trIn is for, and the reason ProjectFactory::createDefault
    takes a locale rather than reading the active one.
*/
juce::String numbered (StringId id, int number)
{
    return trIn (referenceLocale(), id, Args {}.with ("number", number));
}

} // namespace

juce::ValueTree scaffold (int numChannels)
{
    const auto count = juce::jmax (1, numChannels);

    auto project = defaultTreeFor (projectSpec());
    project.setProperty (ids::name, trIn (referenceLocale(), StringId::project_untitled), nullptr);
    project.setProperty (ids::tempoBpm, 128.0, nullptr);

    for (int i = 1; i <= count; ++i)
        project.appendChild (makeChannel (i, numbered (StringId::project_channelN, i),
                                          entityColour::defaultHex (i - 1), 60, "saw", 0, 0.005,
                                          0.120, 0.700, 0.150),
                             nullptr);

    project.appendChild (makePattern (1, "Pattern 1", 16), nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto mixer = project.getChildWithName (ids::MIXER);

    for (int i = 1; i <= count; ++i)
    {
        playlist.appendChild (makePlaylistTrack (numbered (StringId::project_trackN, i)), nullptr);
        mixer.appendChild (makeMixerTrack (i, numbered (StringId::project_insertN, i)), nullptr);
    }

    return canonicalTree (project, projectSpec());
}

void setSong (juce::ValueTree project, const juce::String& name, double tempoBpm, int bars)
{
    project.setProperty (ids::name, name, nullptr);
    project.setProperty (ids::tempoBpm, stored (tempoBpm), nullptr);
    project.setProperty (ids::barsInSong, bars, nullptr);
}

void setGrid (juce::ValueTree project, int stepsPerBeat, int beatsPerBar, int beatUnit)
{
    project.setProperty (ids::stepsPerBeat, stepsPerBeat, nullptr);
    project.setProperty (ids::beatsPerBar, beatsPerBar, nullptr);
    project.setProperty (ids::beatUnit, beatUnit, nullptr);
}

juce::ValueTree channelWithId (const juce::ValueTree& project, int id)
{
    return tree::childWithId (project, ids::CHANNEL, id);
}

juce::ValueTree channelNamed (const juce::ValueTree& project, const juce::String& name)
{
    for (const auto& channel : project)
        if (channel.hasType (ids::CHANNEL) && channel[ids::name].toString().equalsIgnoreCase (name))
            return channel;

    return {};
}

juce::ValueTree mixerTrackWithId (const juce::ValueTree& project, int id)
{
    return tree::childWithId (project.getChildWithName (ids::MIXER), ids::MIXER_TRACK, id);
}

juce::ValueTree masterOf (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
}

void setMixerTrack (juce::ValueTree project, int id, double gain, double pan)
{
    if (auto track = mixerTrackWithId (project, id); track.isValid())
    {
        track.setProperty (ids::gain, stored (gain), nullptr);
        track.setProperty (ids::pan, stored (pan), nullptr);
    }
}

juce::ValueTree laneAt (const juce::ValueTree& project, int index)
{
    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto seen = 0;

    for (const auto& lane : playlist)
        if (lane.hasType (ids::PLAYLIST_TRACK) && seen++ == index)
            return lane;

    return {};
}

void setLane (juce::ValueTree project, int index, bool muted, double gain)
{
    if (auto lane = laneAt (project, index); lane.isValid())
    {
        lane.setProperty (ids::mute, muted, nullptr);
        lane.setProperty (ids::gain, stored (gain), nullptr);
    }
}

void pruneUnplayedChannels (juce::ValueTree project)
{
    juce::Array<int> played;

    for (const auto& pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        for (const auto& note : pattern)
            if (note.hasType (ids::NOTE))
                played.addIfNotAlreadyThere ((int) note[ids::ch]);
    }

    // Backwards: removing a child shifts every index after it.
    for (int i = project.getNumChildren(); --i >= 0;)
    {
        const auto child = project.getChild (i);

        if (child.hasType (ids::CHANNEL) && ! played.contains ((int) child[ids::id]))
            project.removeChild (i, nullptr);

        // The empty pattern a fresh project starts with, once a score has
        // written its own. Left in, the piano roll opens on sixteen blank steps.
        if (child.hasType (ids::PATTERN) && child.getNumChildren() == 0)
        {
            bool anyOther = false;

            for (const auto& other : project)
                if (other.hasType (ids::PATTERN) && other != child && other.getNumChildren() > 0)
                    anyOther = true;

            if (anyOther)
                project.removeChild (i, nullptr);
        }
    }
}

void pruneEmptyLanes (juce::ValueTree project)
{
    auto playlist = project.getChildWithName (ids::PLAYLIST);

    for (int i = playlist.getNumChildren(); --i >= 0;)
    {
        const auto lane = playlist.getChild (i);

        if (lane.hasType (ids::PLAYLIST_TRACK) && lane.getNumChildren() == 0)
            playlist.removeChild (i, nullptr);
    }

    // Never none: the playlist is what the arrangement is drawn on, and a
    // project with no lane at all has nowhere to drop the first clip.
    if (playlist.getNumChildren() == 0)
        playlist.appendChild (makePlaylistTrack ("Track 1"), nullptr);
}

void setInserts (juce::ValueTree project, const juce::StringArray& names)
{
    auto mixer = project.getChildWithName (ids::MIXER);

    if (! mixer.isValid())
        return;

    // The existing tracks come out first, chains and all: a caller that put an
    // effect on an insert before calling this would otherwise lose it, and an
    // order of operations that mattered would be a trap rather than a rule.
    juce::Array<juce::ValueTree> kept;

    for (int i = mixer.getNumChildren(); --i >= 0;)
        if (mixer.getChild (i).hasType (ids::MIXER_TRACK))
        {
            kept.insert (0, mixer.getChild (i));
            mixer.removeChild (i, nullptr);
        }

    for (int i = 0; i < names.size(); ++i)
    {
        const auto id = i + 1;
        auto track = i < kept.size() ? kept.getReference (i) : makeMixerTrack (id, names[i]);

        track.setProperty (ids::id, id, nullptr);
        track.setProperty (ids::name, names[i], nullptr);
        mixer.appendChild (track, nullptr);
    }
}

void setLanes (juce::ValueTree project, const juce::StringArray& names)
{
    auto playlist = project.getChildWithName (ids::PLAYLIST);

    if (! playlist.isValid())
        return;

    juce::Array<juce::ValueTree> kept;

    for (int i = playlist.getNumChildren(); --i >= 0;)
        if (playlist.getChild (i).hasType (ids::PLAYLIST_TRACK))
        {
            kept.insert (0, playlist.getChild (i));
            playlist.removeChild (i, nullptr);
        }

    for (int i = 0; i < names.size(); ++i)
    {
        auto lane = i < kept.size() ? kept.getReference (i) : makePlaylistTrack (names[i]);
        lane.setProperty (ids::name, names[i], nullptr);
        playlist.appendChild (lane, nullptr);
    }
}

void rebuildInserts (juce::ValueTree project)
{
    juce::Array<juce::ValueTree> channels;
    juce::StringArray names;

    for (const auto& channel : project)
        if (channel.hasType (ids::CHANNEL))
        {
            channels.add (channel);
            names.add (channel[ids::name].toString());
        }

    setInserts (project, names);

    for (int i = 0; i < channels.size(); ++i)
        routeTo (channels.getReference (i), i + 1);
}

} // namespace dew::demo
