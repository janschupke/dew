#include "model/DemoBuilders.h"

#include "model/EntityColour.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"

namespace dew::demo
{

namespace
{

const NodeSpec& channelsSpec()
{
    return childSpecFor (projectSpec(), "channels");
}
const NodeSpec& patternsSpec()
{
    return childSpecFor (projectSpec(), "patterns");
}
const NodeSpec& playlistSpec()
{
    return childSpecFor (projectSpec(), "playlist");
}
const NodeSpec& mixerSpec()
{
    return childSpecFor (projectSpec(), "mixer");
}
const NodeSpec& automationSpec()
{
    return childSpecFor (projectSpec(), "automations");
}

const NodeSpec& tracksSpec()
{
    return childSpecFor (playlistSpec(), "tracks");
}
const NodeSpec& clipsSpec()
{
    return childSpecFor (tracksSpec(), "clips");
}
const NodeSpec& effectsSpec()
{
    return childSpecFor (channelsSpec(), "effects");
}
const NodeSpec& notesSpec()
{
    return childSpecFor (patternsSpec(), "notes");
}
const NodeSpec& pointsSpec()
{
    return childSpecFor (automationSpec(), "points");
}

juce::ValueTree oscSlot (juce::ValueTree channel, int slot)
{
    return ProjectEdits::oscillatorAt (channel, slot);
}

} // namespace

juce::ValueTree makeChannel (int id, const juce::String& name, const juce::String& colour,
                             int basePitch, const juce::String& wave, int octave, double attack,
                             double decay, double sustain, double release, double volume)
{
    auto channel = defaultTreeFor (channelsSpec());

    channel.setProperty (ids::id, id, nullptr);
    channel.setProperty (ids::name, name, nullptr);
    channel.setProperty (ids::colour, colour, nullptr);
    channel.setProperty (ids::mixerTrackId, id, nullptr);
    channel.setProperty (ids::basePitch, basePitch, nullptr);
    channel.setProperty (ids::volume, volume, nullptr);

    auto osc = ProjectEdits::oscillatorAt (channel, 0);
    osc.setProperty (ids::wave, wave, nullptr);
    osc.setProperty (ids::octave, octave, nullptr);

    setAmp (channel, attack, decay, sustain, release);

    return channel;
}

juce::ValueTree makeMixerTrack (int id, const juce::String& name)
{
    auto track = defaultTreeFor (childSpecFor (mixerSpec(), "tracks"));
    track.setProperty (ids::id, id, nullptr);
    track.setProperty (ids::name, name, nullptr);
    track.setProperty (ids::gain, 0.8, nullptr);
    track.setProperty (ids::pan, 0.0, nullptr);
    track.setProperty (ids::mute, false, nullptr);
    return track;
}

juce::ValueTree makePlaylistTrack (const juce::String& name)
{
    auto track = defaultTreeFor (tracksSpec());
    track.setProperty (ids::name, name, nullptr);
    return track;
}

juce::ValueTree makePattern (int id, const juce::String& name, int lengthSteps)
{
    auto pattern = defaultTreeFor (patternsSpec());
    pattern.setProperty (ids::id, id, nullptr);
    pattern.setProperty (ids::name, name, nullptr);
    pattern.setProperty (ids::lengthSteps, lengthSteps, nullptr);
    return pattern;
}

juce::ValueTree makeNote (int channelId, int step, int lengthSteps, int pitch, double velocity)
{
    auto note = defaultTreeFor (notesSpec());
    note.setProperty (ids::ch, channelId, nullptr);
    note.setProperty (ids::step, step, nullptr);
    note.setProperty (ids::lengthSteps, lengthSteps, nullptr);
    note.setProperty (ids::pitch, pitch, nullptr);
    note.setProperty (ids::velocity, velocity, nullptr);
    return note;
}

juce::ValueTree patternIn (juce::ValueTree project, int id, const juce::String& name,
                           int lengthSteps)
{
    for (auto pattern : project)
        if (pattern.hasType (ids::PATTERN) && (int) pattern[ids::id] == id)
        {
            pattern.setProperty (ids::name, name, nullptr);
            pattern.setProperty (ids::lengthSteps, lengthSteps, nullptr);
            return pattern;
        }

    auto pattern = makePattern (id, name, lengthSteps);
    project.appendChild (pattern, nullptr);
    return pattern;
}

juce::ValueTree makeClip (int patternId, int startBar, int lengthBars)
{
    auto clip = defaultTreeFor (clipsSpec());
    clip.setProperty (ids::kind, "pattern", nullptr);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startBar, startBar, nullptr);
    clip.setProperty (ids::lengthBars, lengthBars, nullptr);
    return clip;
}

juce::ValueTree makeAutomationClip (int automationId, int startBar, int lengthBars)
{
    auto clip = defaultTreeFor (clipsSpec());
    clip.setProperty (ids::kind, "automation", nullptr);
    clip.setProperty (ids::automationId, automationId, nullptr);
    clip.setProperty (ids::startBar, startBar, nullptr);
    clip.setProperty (ids::lengthBars, lengthBars, nullptr);
    return clip;
}

juce::ValueTree makeEffect (int id, const juce::String& type, Params params)
{
    auto effect = defaultTreeFor (effectsSpec());
    effect.setProperty (ids::id, id, nullptr);
    effect.setProperty (ids::type, type, nullptr);

    for (const auto& [property, value] : params)
        effect.setProperty (property, value, nullptr);

    return effect;
}

juce::ValueTree makeAutomation (int id, const juce::String& name, AutomationScope scope,
                                int targetId, int slot, const juce::Identifier& param,
                                std::initializer_list<Point> points)
{
    auto automation = defaultTreeFor (automationSpec());
    automation.setProperty (ids::id, id, nullptr);
    automation.setProperty (ids::name, name, nullptr);
    automation.setProperty (ids::scope, automationScopeToString (scope), nullptr);
    automation.setProperty (ids::targetId, targetId, nullptr);
    automation.setProperty (ids::slot, slot, nullptr);
    automation.setProperty (ids::param, param.toString(), nullptr);

    for (const auto& point : points)
    {
        auto node = defaultTreeFor (pointsSpec());
        node.setProperty (ids::step, point.step, nullptr);
        node.setProperty (ids::value, point.value, nullptr);
        node.setProperty (ids::curve, point.curve, nullptr);
        node.setProperty (ids::shape, point.shape, nullptr);
        automation.appendChild (node, nullptr);
    }

    return automation;
}

juce::ValueTree scaffold (int numChannels)
{
    const auto count = juce::jmax (1, numChannels);

    auto project = defaultTreeFor (projectSpec());
    project.setProperty (ids::name, "Untitled", nullptr);
    project.setProperty (ids::tempoBpm, 128.0, nullptr);

    for (int i = 1; i <= count; ++i)
        project.appendChild (makeChannel (i, "Channel " + juce::String (i),
                                          entityColour::defaultHex (i - 1), 60, "saw", 0, 0.005,
                                          0.120, 0.700, 0.150),
                             nullptr);

    project.appendChild (makePattern (1, "Pattern 1", 16), nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto mixer = project.getChildWithName (ids::MIXER);

    for (int i = 1; i <= count; ++i)
    {
        playlist.appendChild (makePlaylistTrack ("Track " + juce::String (i)), nullptr);
        mixer.appendChild (makeMixerTrack (i, "Insert " + juce::String (i)), nullptr);
    }

    return canonicalTree (project, projectSpec());
}

juce::ValueTree channelWithId (const juce::ValueTree& project, int id)
{
    for (const auto& channel : project)
        if (channel.hasType (ids::CHANNEL) && (int) channel[ids::id] == id)
            return channel;

    return {};
}

juce::ValueTree channelNamed (const juce::ValueTree& project, const juce::String& name)
{
    for (const auto& channel : project)
        if (channel.hasType (ids::CHANNEL) && channel[ids::name].toString().equalsIgnoreCase (name))
            return channel;

    return {};
}

void setClassicOsc (juce::ValueTree channel, int slot, const juce::String& wave, int octave,
                    double gain, int detuneCents)
{
    auto osc = oscSlot (channel, slot);

    if (! osc.isValid())
        return;

    osc.setProperty (ids::enabled, true, nullptr);
    osc.setProperty (ids::mode, "classic", nullptr);
    osc.setProperty (ids::wave, wave, nullptr);
    osc.setProperty (ids::octave, octave, nullptr);
    osc.setProperty (ids::detuneCents, detuneCents, nullptr);
    osc.setProperty (ids::gain, gain, nullptr);
}

void setWavetableOsc (juce::ValueTree channel, int slot, const juce::String& table, double position,
                      double mod, const juce::String& source, double rate, int unisonVoices,
                      double unisonDetune, int octave, double gain)
{
    auto osc = oscSlot (channel, slot);

    if (! osc.isValid())
        return;

    osc.setProperty (ids::enabled, true, nullptr);
    osc.setProperty (ids::mode, "wavetable", nullptr);
    osc.setProperty (ids::wavetable, table, nullptr);
    osc.setProperty (ids::wavePosition, position, nullptr);
    osc.setProperty (ids::wavePositionMod, mod, nullptr);
    osc.setProperty (ids::wavePositionSource, source, nullptr);
    osc.setProperty (ids::wavePositionRate, rate, nullptr);
    osc.setProperty (ids::unisonVoices, unisonVoices, nullptr);
    osc.setProperty (ids::unisonDetune, unisonDetune, nullptr);
    osc.setProperty (ids::octave, octave, nullptr);
    osc.setProperty (ids::gain, gain, nullptr);
}

void disableOsc (juce::ValueTree channel, int slot)
{
    if (auto osc = oscSlot (channel, slot); osc.isValid())
        osc.setProperty (ids::enabled, false, nullptr);
}

void setAmp (juce::ValueTree channel, double attack, double decay, double sustain, double release)
{
    auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);

    if (! amp.isValid())
        return;

    amp.setProperty (ids::attack, attack, nullptr);
    amp.setProperty (ids::decay, decay, nullptr);
    amp.setProperty (ids::sustain, sustain, nullptr);
    amp.setProperty (ids::release, release, nullptr);
}

void routeTo (juce::ValueTree channel, int mixerTrackId)
{
    if (channel.isValid())
        channel.setProperty (ids::mixerTrackId, mixerTrackId, nullptr);
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
