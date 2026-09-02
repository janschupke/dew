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

        snapshot.channels.push_back (c);
    }

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
    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (const auto& clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            ClipSnapshot c;
            c.patternIndex = snapshot.patternIndexForId ((int) clip[ids::patternId]);
            c.startBar     = juce::jmax (0, (int) clip[ids::startBar]);
            c.lengthBars   = juce::jmax (1, (int) clip[ids::lengthBars]);

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
