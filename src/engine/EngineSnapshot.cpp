#include "EngineSnapshot.h"

#include <atomic>

#include "../model/Ids.h"

namespace dew
{

Waveform waveformFromString (const juce::String& s)
{
    if (s == "sine")     return Waveform::sine;
    if (s == "square")   return Waveform::square;
    if (s == "triangle") return Waveform::triangle;
    return Waveform::saw;
}

juce::String waveformToString (Waveform w)
{
    switch (w)
    {
        case Waveform::sine:     return "sine";
        case Waveform::square:   return "square";
        case Waveform::triangle: return "triangle";
        case Waveform::saw:      break;
    }
    return "saw";
}

int EngineSnapshot::patternIndexForId (int patternId) const
{
    for (size_t i = 0; i < patterns.size(); ++i)
        if (patterns[i].id == patternId)
            return (int) i;

    return -1;
}

int EngineSnapshot::channelIndexForId (int channelId) const
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i].id == channelId)
            return (int) i;

    return -1;
}

int EngineSnapshot::songLengthSteps() const
{
    int end = 0;

    for (const auto& clip : clips)
        if (clip.patternIndex >= 0)
            end = juce::jmax (end, (clip.startBar + clip.lengthBars) * stepsPerBar());

    return end;
}

bool EngineSnapshot::isChannelAudible (const ChannelSnapshot& channel) const noexcept
{
    // Mute wins over solo on the same channel: mute is the explicit "off".
    if (channel.muted)
        return false;

    return anyChannelSolo ? channel.solo : true;
}

bool EngineSnapshot::isSilent() const
{
    if (channels.empty())
        return true;

    for (const auto& pattern : patterns)
        for (const auto& note : pattern.notes)
            if (note.channelIndex >= 0)
                return false;

    return true;
}

namespace
{

std::atomic<juce::uint64> nextGeneration { 1 };

OscSettings readOsc (const juce::ValueTree& osc)
{
    OscSettings s;
    s.wave        = waveformFromString (osc[ids::wave].toString());
    s.octave      = juce::jlimit (-4, 4, (int) osc[ids::octave]);
    s.detuneCents = juce::jlimit (-1200.0f, 1200.0f, (float) (double) osc[ids::detuneCents]);
    s.gain        = juce::jlimit (0.0f, 1.0f, (float) (double) osc[ids::gain]);
    return s;
}

AmpSettings readAmp (const juce::ValueTree& amp)
{
    AmpSettings s;
    // A zero attack clicks and a zero release cuts abruptly; clamp to something
    // short rather than to zero.
    s.attack  = juce::jlimit (0.0005f, 10.0f, (float) (double) amp[ids::attack]);
    s.decay   = juce::jlimit (0.0005f, 10.0f, (float) (double) amp[ids::decay]);
    s.sustain = juce::jlimit (0.0f,    1.0f,  (float) (double) amp[ids::sustain]);
    s.release = juce::jlimit (0.0020f, 10.0f, (float) (double) amp[ids::release]);
    return s;
}


/** Assigns a pool unit to an effect id.

    Open addressing on the id, over the units already claimed while building
    this snapshot. The result depends only on the set of effect ids in the
    document, so it is stable across rebuilds: editing a parameter, adding a
    channel or renaming a pattern all leave every effect on the unit it was
    already using, and its reverb tail or delay repeats survive.
*/
int claimEffectUnit (int effectId, std::array<int, kMaxEffectUnits>& owners)
{
    const auto start = ((effectId % kMaxEffectUnits) + kMaxEffectUnits) % kMaxEffectUnits;

    for (int probe = 0; probe < kMaxEffectUnits; ++probe)
    {
        const auto index = (start + probe) % kMaxEffectUnits;

        if (owners[(size_t) index] == effectId || owners[(size_t) index] < 0)
        {
            owners[(size_t) index] = effectId;
            return index;
        }
    }

    return -1;
}

EffectParams readEffectParams (const juce::ValueTree& effect)
{
    EffectParams p;

    p.mix        = juce::jlimit (0.0f, 1.0f, (float) (double) effect[ids::mix]);
    p.filterMode = filterModeFromString (effect[ids::filterMode].toString());
    p.cutoff     = juce::jlimit (20.0f, 20000.0f, (float) (double) effect[ids::cutoff]);
    p.resonance  = juce::jlimit (0.05f, 4.0f, (float) (double) effect[ids::resonance]);
    p.roomSize   = juce::jlimit (0.0f, 1.0f, (float) (double) effect[ids::roomSize]);
    p.damping    = juce::jlimit (0.0f, 1.0f, (float) (double) effect[ids::damping]);
    p.width      = juce::jlimit (0.0f, 1.0f, (float) (double) effect[ids::width]);
    p.delayMs    = juce::jlimit (1.0f, EffectUnit::maxDelayMs, (float) (double) effect[ids::delayMs]);
    p.feedback   = juce::jlimit (0.0f, 0.95f, (float) (double) effect[ids::feedback]);
    p.drive      = juce::jlimit (1.0f, 40.0f, (float) (double) effect[ids::drive]);
    p.outputGain = juce::jlimit (0.0f, 4.0f, (float) (double) effect[ids::outputGain]);
    p.rate       = juce::jlimit (0.01f, 20.0f, (float) (double) effect[ids::rate]);
    p.depth      = juce::jlimit (0.0f, 1.0f, (float) (double) effect[ids::depth]);
    p.lowGainDb  = juce::jlimit (-24.0f, 24.0f, (float) (double) effect[ids::lowGainDb]);
    p.midGainDb  = juce::jlimit (-24.0f, 24.0f, (float) (double) effect[ids::midGainDb]);
    p.midFreq    = juce::jlimit (100.0f, 8000.0f, (float) (double) effect[ids::midFreq]);
    p.highGainDb = juce::jlimit (-24.0f, 24.0f, (float) (double) effect[ids::highGainDb]);

    return p;
}

/** Reads a chain off any node that can carry one. `ownerName` is only used for
    warnings, so a dropped effect says which chain it was in.
*/
EffectChainSnapshot readEffectChain (const juce::ValueTree& owner, const juce::String& ownerName,
                                     std::array<int, kMaxEffectUnits>& unitOwners,
                                     const std::function<void (const juce::String&)>& warn)
{
    EffectChainSnapshot chain;

    for (const auto& effect : owner)
    {
        if (! effect.hasType (ids::EFFECT))
            continue;

        if (chain.numSlots >= kMaxEffectsPerChain)
        {
            warn (ownerName + " has more than " + juce::String (kMaxEffectsPerChain)
                  + " effects; the rest are not rendered.");
            break;
        }

        EffectSnapshot slot;
        slot.id = (int) effect[ids::id];
        slot.type = effectTypeFromString (effect[ids::type].toString());
        slot.enabled = (bool) effect[ids::enabled];
        slot.params = readEffectParams (effect);
        slot.unitIndex = claimEffectUnit ((int) effect[ids::id], unitOwners);

        if (slot.unitIndex < 0)
        {
            warn ("More than " + juce::String (kMaxEffectUnits)
                  + " effects in the project; " + ownerName + " is not fully rendered.");
            break;
        }

        chain.slots[(size_t) chain.numSlots++] = slot;
    }

    return chain;
}

} // namespace

EngineSnapshot buildSnapshot (const juce::ValueTree& project, juce::StringArray* warnings)
{
    const auto warn = [warnings] (const juce::String& message)
    {
        if (warnings != nullptr)
            warnings->add (message);
    };

    EngineSnapshot snapshot;
    snapshot.generation = nextGeneration.fetch_add (1, std::memory_order_relaxed);

    if (! project.isValid())
        return snapshot;

    snapshot.tempoBpm     = juce::jlimit (20.0, 999.0, (double) project[ids::tempoBpm]);
    snapshot.stepsPerBeat = juce::jlimit (1, 16, (int) project[ids::stepsPerBeat]);
    snapshot.barsInSong   = juce::jmax (1, (int) project[ids::barsInSong]);

    // Which pool unit each effect id has claimed, for the whole project. -1 is
    // free; the map is rebuilt from scratch every time, and is a pure function
    // of the ids present, so it comes out identical for an unchanged document.
    std::array<int, kMaxEffectUnits> unitOwners;
    unitOwners.fill (-1);

    // --- mixer ---------------------------------------------------------------
    const auto mixer = project.getChildWithName (ids::MIXER);
    snapshot.masterGain = juce::jlimit (0.0f, 2.0f,
                                        (float) (double) mixer.getChildWithName (ids::MASTER)[ids::gain]);

    for (const auto& track : mixer)
    {
        if (! track.hasType (ids::MIXER_TRACK))
            continue;

        if ((int) snapshot.mixerTracks.size() >= kMaxMixerTracks)
        {
            warn ("More than " + juce::String (kMaxMixerTracks) + " mixer tracks; the rest are not rendered.");
            break;
        }

        MixerTrackSnapshot m;
        m.id   = (int) track[ids::id];
        m.gain = juce::jlimit (0.0f, 2.0f, (float) (double) track[ids::gain]);
        m.pan  = juce::jlimit (-1.0f, 1.0f, (float) (double) track[ids::pan]);
        m.mute = (bool) track[ids::mute];
        m.solo = (bool) track[ids::solo];

        m.effects = readEffectChain (track, "Mixer track " + track[ids::name].toString(),
                                     unitOwners, warn);

        snapshot.anySolo = snapshot.anySolo || m.solo;
        snapshot.mixerTracks.push_back (m);
    }

    // --- channels ------------------------------------------------------------
    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        if ((int) snapshot.channels.size() >= kMaxChannels)
        {
            warn ("More than " + juce::String (kMaxChannels) + " channels; the rest are not rendered.");
            break;
        }

        ChannelSnapshot c;
        c.id        = (int) channel[ids::id];
        c.basePitch = juce::jlimit (0, 127, (int) channel[ids::basePitch]);
        c.volume    = juce::jlimit (0.0f, 1.0f, (float) (double) channel[ids::volume]);
        c.pan       = juce::jlimit (-1.0f, 1.0f, (float) (double) channel[ids::pan]);
        c.muted     = (bool) channel[ids::muted];
        c.solo      = (bool) channel[ids::solo];

        snapshot.anyChannelSolo = snapshot.anyChannelSolo || c.solo;

        const auto instrument = channel.getChildWithName (ids::INSTRUMENT);
        c.osc = readOsc (instrument.getChildWithName (ids::OSC));
        c.amp = readAmp (instrument.getChildWithName (ids::AMP));

        // Resolve the mixer routing now; the audio thread must not search.
        const int mixerTrackId = (int) channel[ids::mixerTrackId];
        c.mixerTrackIndex = -1;

        for (size_t i = 0; i < snapshot.mixerTracks.size(); ++i)
            if (snapshot.mixerTracks[i].id == mixerTrackId)
                c.mixerTrackIndex = (int) i;

        if (c.mixerTrackIndex < 0 && ! snapshot.mixerTracks.empty())
        {
            warn ("Channel \"" + channel[ids::name].toString() + "\" routes to mixer track "
                  + juce::String (mixerTrackId) + ", which does not exist; using the first insert.");
            c.mixerTrackIndex = 0;
        }

        c.effects = readEffectChain (channel, "Channel \"" + channel[ids::name].toString() + "\"",
                                     unitOwners, warn);

        snapshot.channels.push_back (c);
    }

    // Lets the engine skip the whole stereo effect stage on a project with none.
    const auto chainHasWork = [] (const EffectChainSnapshot& chain)
    {
        for (int i = 0; i < chain.numSlots; ++i)
            if (chain.slots[(size_t) i].enabled)
                return true;

        return false;
    };

    for (const auto& channel : snapshot.channels)
        snapshot.anyEffects = snapshot.anyEffects || chainHasWork (channel.effects);

    for (const auto& track : snapshot.mixerTracks)
        snapshot.anyEffects = snapshot.anyEffects || chainHasWork (track.effects);

    // --- patterns ------------------------------------------------------------
    for (const auto& pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        PatternSnapshot p;
        p.id          = (int) pattern[ids::id];
        p.lengthSteps = juce::jmax (1, (int) pattern[ids::lengthSteps]);

        for (const auto& note : pattern)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            NoteSnapshot n;
            n.channelIndex = snapshot.channelIndexForId ((int) note[ids::ch]);
            n.step         = juce::jmax (0, (int) note[ids::step]);
            n.lengthSteps  = juce::jmax (1, (int) note[ids::lengthSteps]);
            n.pitch        = juce::jlimit (0, 127, (int) note[ids::pitch]);
            n.velocity     = juce::jlimit (0.0f, 1.0f, (float) (double) note[ids::velocity]);

            if (n.channelIndex < 0)
            {
                warn ("A note in pattern " + juce::String (p.id) + " refers to channel "
                      + note[ids::ch].toString() + ", which does not exist; it will not sound.");
                continue;
            }

            // A note starting past the end of its pattern would never play.
            if (n.step >= p.lengthSteps)
            {
                warn ("A note in pattern " + juce::String (p.id) + " starts at step "
                      + juce::String (n.step) + ", past the pattern's length; it will not sound.");
                continue;
            }

            p.notes.push_back (n);
        }

        snapshot.patterns.push_back (p);
    }

    // --- playlist ------------------------------------------------------------
    // Solo has to be known before any clip is resolved, so scan for it first.
    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK) && (bool) track[ids::solo])
            snapshot.anyPlaylistTrackSolo = true;

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        const auto trackAudible = ! (bool) track[ids::mute]
                                  && (! snapshot.anyPlaylistTrackSolo || (bool) track[ids::solo]);

        for (const auto& clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            ClipSnapshot c;
            c.patternIndex = snapshot.patternIndexForId ((int) clip[ids::patternId]);
            c.startBar     = juce::jmax (0, (int) clip[ids::startBar]);
            c.lengthBars   = juce::jmax (1, (int) clip[ids::lengthBars]);
            c.trackAudible = trackAudible;

            if (c.patternIndex < 0)
            {
                warn ("A clip refers to pattern " + clip[ids::patternId].toString()
                      + ", which does not exist; it will not play.");
                continue;
            }

            snapshot.clips.push_back (c);
        }
    }

    return snapshot;
}

} // namespace dew
