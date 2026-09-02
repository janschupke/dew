#include "engine/EngineSnapshot.h"

#include "model/AssetPaths.h"
#include "engine/SampleProvider.h"
#include "engine/Wavetable.h"

#include <atomic>
#include <cmath>
#include <functional>

#include "model/Ids.h"
#include "model/Meter.h"

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

OscMode oscModeFromString (const juce::String& s)
{
    return s == "wavetable" ? OscMode::wavetable : OscMode::classic;
}

juce::String oscModeToString (OscMode m)
{
    switch (m)
    {
        case OscMode::wavetable: return "wavetable";
        case OscMode::classic:   break;
    }
    return "classic";
}

PositionSource positionSourceFromString (const juce::String& s)
{
    return s == "lfo" ? PositionSource::lfo : PositionSource::envelope;
}

juce::String positionSourceToString (PositionSource p)
{
    switch (p)
    {
        case PositionSource::lfo:      return "lfo";
        case PositionSource::envelope: break;
    }
    return "envelope";
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

    // Every kind of clip counts. A sweep placed after the last note is still
    // part of the arrangement, and leaving automation out made the song loop
    // out from underneath it; an arrangement of nothing but recordings has the
    // same problem in a starker form - it would have no length at all.
    for (const auto& clip : clips)
        if (clip.patternIndex >= 0 || clip.automationIndex >= 0 || clip.channelIndex >= 0)
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

    // An audio-only project has no notes at all. Without this it reports
    // "nothing to play", and dew_render exits non-zero on a perfectly good
    // arrangement of recordings.
    for (const auto& clip : clips)
        if (clip.channelIndex >= 0
            && channels[(size_t) clip.channelIndex].audio != nullptr)
            return false;

    return true;
}

namespace
{

std::atomic<juce::uint64> nextGeneration { 1 };

OscBankSnapshot readOscBank (const juce::ValueTree& instrument,
                             const juce::String& ownerName,
                             const std::function<void (const juce::String&)>& warn)
{
    OscBankSnapshot bank;

    for (const auto& osc : instrument)
    {
        if (! osc.hasType (ids::OSC))
            continue;

        if (bank.numSlots >= kMaxOscillators)
        {
            warn (ownerName + " has more than " + juce::String (kMaxOscillators)
                  + " oscillators; the rest are not rendered.");
            break;
        }

        auto& s = bank.slots[(size_t) bank.numSlots++];
        s.enabled     = (bool) osc.getProperty (ids::enabled, true);
        s.mode        = oscModeFromString (osc[ids::mode].toString());
        s.wave        = waveformFromString (osc[ids::wave].toString());
        s.octave      = juce::jlimit (-4, 4, (int) osc[ids::octave]);
        s.detuneCents = juce::jlimit (-1200.0f, 1200.0f, (float) (double) osc[ids::detuneCents]);
        s.gain        = juce::jlimit (0.0f, 1.0f, (float) (double) osc[ids::gain]);

        const auto tableName = osc[ids::wavetable].toString();
        const auto tableIndex = wavetableIndexFor (tableName);

        // A name this build does not know is a fault in the FILE, not a
        // different sound: say so rather than quietly play something else.
        if (tableIndex < 0 && s.mode == OscMode::wavetable)
            warn (ownerName + " asks for wavetable \"" + tableName
                  + "\", which this build does not have; using the first one.");

        s.table          = juce::jmax (0, tableIndex);
        s.position       = juce::jlimit (0.0f, 1.0f, (float) (double) osc[ids::wavePosition]);
        s.positionMod    = juce::jlimit (-1.0f, 1.0f, (float) (double) osc[ids::wavePositionMod]);
        s.positionSource = positionSourceFromString (osc[ids::wavePositionSource].toString());
        s.positionRate   = juce::jlimit (0.01f, 20.0f, (float) (double) osc[ids::wavePositionRate]);
        s.unisonVoices   = juce::jlimit (1, kMaxUnisonVoices, (int) osc[ids::unisonVoices]);
        s.unisonDetune   = juce::jlimit (0.0f, 50.0f, (float) (double) osc[ids::unisonDetune]);

        bank.anyEnabled = bank.anyEnabled || s.enabled;
    }

    // An instrument carrying no oscillator node at all - one a test assembled by
    // hand, not one the schema produced - gets a single default one, which is
    // what reading a missing node used to yield. Silence here would make a
    // missing child indistinguishable from a channel someone switched off.
    if (bank.numSlots == 0)
    {
        bank.slots[0] = {};
        bank.numSlots = 1;
        bank.anyEnabled = true;
    }

    return bank;
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

/** Which parameter an automation clip drives, from the property it names.

    Keyed on the ids:: identifiers rather than on twenty string literals. Those
    literals were the only place in production that spelled a property name by
    hand, and the failure mode was quiet in the worst way: renaming an
    identifier in Ids.h compiled cleanly everywhere, the schema and the editor
    followed the new name, and every automation curve pointed at the old one
    simply stopped doing anything. Nothing warned, because a name that matches
    nothing is indistinguishable from an automation of nothing.

    Now there is nothing to keep in step - a rename moves the identifier this
    table already points at.
*/
AutomationParam automationParamFromIdentifier (const juce::Identifier& property)
{
    static const std::pair<const juce::Identifier*, AutomationParam> table[] {
        { &ids::volume, AutomationParam::volume }, { &ids::pan, AutomationParam::pan },
        { &ids::gain, AutomationParam::gain }, { &ids::cutoff, AutomationParam::cutoff },
        { &ids::wavePosition, AutomationParam::position },
        { &ids::resonance, AutomationParam::resonance }, { &ids::mix, AutomationParam::mix },
        { &ids::roomSize, AutomationParam::roomSize }, { &ids::damping, AutomationParam::damping },
        { &ids::width, AutomationParam::width }, { &ids::delayMs, AutomationParam::delayMs },
        { &ids::feedback, AutomationParam::feedback }, { &ids::drive, AutomationParam::drive },
        { &ids::outputGain, AutomationParam::outputGain }, { &ids::rate, AutomationParam::rate },
        { &ids::depth, AutomationParam::depth }, { &ids::lowGainDb, AutomationParam::lowGainDb },
        { &ids::midGainDb, AutomationParam::midGainDb }, { &ids::midFreq, AutomationParam::midFreq },
        { &ids::highGainDb, AutomationParam::highGainDb },
    };

    for (const auto& [id, value] : table)
        if (property == *id)
            return value;

    return AutomationParam::none;
}

EffectParamBlock readEffectParams (const juce::ValueTree& effect, EffectType type)
{
    EffectParamBlock block {};

    const auto write = [&effect, &block] (int index, const ParamSpec& spec)
    {
        if (index < 0 || index >= kMaxEffectParams)
            return;

        if (spec.control == ParamControl::choice)
        {
            const auto text = effect.getProperty (*spec.property, spec.defaultVar()).toString();
            auto choice = 0;

            for (int i = 0; i < spec.numChoices; ++i)
                if (text == spec.choices[i].id)
                    choice = i;

            block[(size_t) index] = (float) choice;
            return;
        }

        // The DEFAULT matters as much as the clamp. A missing property used to
        // read as a void var, which became 0.0 - and for `mix` that meant a
        // slot that silently bypassed itself.
        block[(size_t) index] = spec.clamp ((float) (double) effect.getProperty (*spec.property,
                                                                                 spec.defaultVar()));
    };

    for (const auto& spec : commonEffectParams())
        write (effectParamIndex (type, *spec.property), spec);

    const auto& descriptor = effectDescriptor (type);

    for (int i = 0; i < descriptor.numParams; ++i)
        write (effectParamIndex (type, *descriptor.params[i].property), descriptor.params[i]);

    return block;
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

        // An unrecognised type used to become a low-pass filter, silently. A
        // project written by a newer dew then played back wrong with nothing
        // said about it - the one failure in this file that had no warning
        // while its neighbours all did.
        const auto typeName = effect[ids::type].toString();
        const auto type = effectTypeFor (typeName);

        if (! type.has_value())
        {
            warn (ownerName + " has an effect of unknown type \"" + typeName
                  + "\"; it is not rendered.");
            continue;
        }

        EffectSnapshot slot;
        slot.id = (int) effect[ids::id];
        slot.type = *type;
        slot.enabled = (bool) effect[ids::enabled];
        slot.params = readEffectParams (effect, *type);
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

float AutomationSnapshot::valueAt (double step) const noexcept
{
    const auto normalised = [this, step]() -> float
    {
        if (points.empty())
            return 0.0f;

        if (step <= points.front().step)
            return points.front().value;

        if (step >= points.back().step)
            return points.back().value;

        for (size_t i = 1; i < points.size(); ++i)
        {
            if (step > points[i].step)
                continue;

            const auto span = points[i].step - points[i - 1].step;

            if (span <= 0.0)
                return points[i].value;

            auto t = (float) ((step - points[i - 1].step) / span);
            const auto curve = points[i - 1].curve;

            if (! juce::approximatelyEqual (curve, 0.0f))
                t = std::pow (t, std::pow (2.0f, -curve * 2.0f));

            return points[i - 1].value + (points[i].value - points[i - 1].value) * t;
        }

        return points.back().value;
    }();

    // Same mapping the picker and the point editor use, so what is drawn is
    // what is heard.
    const AutomationParamSpec spec { nullptr, "", (double) minimum, (double) maximum,
                                     false, logarithmic };

    return (float) mapAutomationValue (spec, (double) normalised);
}

namespace
{

/** Fills in an audio channel's sample settings, and fetches its audio.

    Every value is clamped against the audio that was actually found rather than
    against what the document claims, so a trim left over from a longer take
    cannot make the render path read off the end of a shorter one.
*/
void readSample (ChannelSnapshot& c, const juce::ValueTree& channel, SampleProvider* samples,
                 const std::function<void (const juce::String&)>& warn)
{
    const auto node = channel.getChildWithName (ids::SAMPLE);

    if (! node.isValid())
        return;

    const auto path = node[ids::file].toString();

    if (path.isEmpty())
        return;

    if (samples == nullptr)
        return;

    auto sourceSampleRate = kDefaultSampleRate;
    auto audio = samples->audioFor (path, sourceSampleRate);

    if (audio == nullptr || audio->getNumSamples() == 0)
    {
        warn ("Channel \"" + channel[ids::name].toString() + "\" refers to audio \"" + path
              + "\", which could not be read; it will not play.");
        return;
    }

    c.audio = audio;

    const auto available = audio->getNumSamples();

    auto& settings = c.sample;
    settings.sourceSampleRate = sourceSampleRate;

    settings.startSample = juce::jlimit (0, available, (int) node[ids::startSample]);

    // 0 means "to the end", which is what an untrimmed sample and a freshly
    // recorded one both store.
    const auto storedEnd = (int) node[ids::endSample];
    settings.endSample = storedEnd <= 0 ? available
                                        : juce::jlimit (settings.startSample, available, storedEnd);

    const auto region = juce::jmax (0, settings.endSample - settings.startSample);

    const auto toFrames = [sourceSampleRate] (double ms)
    {
        return (int) juce::jmax (0.0, ms * 0.001 * sourceSampleRate);
    };

    // Fades are clamped to the region and then to each other: two fades longer
    // than the audio between them would otherwise multiply into a notch rather
    // than degrading to a triangle.
    settings.fadeInSamples = juce::jmin (region, toFrames ((double) node[ids::fadeInMs]));
    settings.fadeOutSamples = juce::jmin (region - settings.fadeInSamples,
                                          toFrames ((double) node[ids::fadeOutMs]));
    settings.fadeOutSamples = juce::jmax (0, settings.fadeOutSamples);

    // The one std::pow, here on the message thread rather than per sample.
    const auto semitones = juce::jlimit (-48.0, 48.0, (double) node[ids::transpose]);
    settings.pitchRatio = (float) std::pow (2.0, semitones / 12.0);

    settings.reverse = (bool) node[ids::reverse];
    settings.loop = (bool) node[ids::loop];
}

} // namespace

EngineSnapshot buildSnapshot (const juce::ValueTree& project, juce::StringArray* warnings,
                              SampleProvider* samples)
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

    // Through Meter rather than read here, so the engine's idea of a bar and
    // the editors' cannot drift apart - both clamp the same way and both treat
    // an absent property as 4/4.
    const auto meter = Meter::of (project);
    snapshot.stepsPerBeat = meter.stepsPerBeat;
    snapshot.beatsPerBar  = meter.beatsPerBar;
    snapshot.beatUnit     = meter.beatUnit;

    // Which pool unit each effect id has claimed, for the whole project. -1 is
    // free; the map is rebuilt from scratch every time, and is a pure function
    // of the ids present, so it comes out identical for an unchanged document.
    std::array<int, kMaxEffectUnits> unitOwners;
    unitOwners.fill (-1);

    // --- mixer ---------------------------------------------------------------
    const auto mixer = project.getChildWithName (ids::MIXER);
    const auto master = mixer.getChildWithName (ids::MASTER);
    snapshot.masterGain = juce::jlimit (0.0f, 2.0f, (float) (double) master[ids::gain]);

    // Before the tracks, so the master's effects claim their pool units first
    // and adding an insert cannot move them.
    snapshot.masterEffects = readEffectChain (master, "Master", unitOwners, warn);

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
        c.volume    = juce::jlimit (0.0f, 1.0f, (float) (double) channel[ids::volume]);
        c.pan       = juce::jlimit (-1.0f, 1.0f, (float) (double) channel[ids::pan]);
        c.muted     = (bool) channel[ids::muted];
        c.solo      = (bool) channel[ids::solo];

        snapshot.anyChannelSolo = snapshot.anyChannelSolo || c.solo;

        const auto instrument = channel.getChildWithName (ids::INSTRUMENT);
        c.osc = readOscBank (instrument,
                             "Channel \"" + channel[ids::name].toString() + "\"", warn);
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

        c.source = channel[ids::source].toString() == "audio" ? InstrumentType::audio
                                                              : InstrumentType::synth;

        if (c.source == InstrumentType::audio)
            readSample (c, channel, samples, warn);

        snapshot.channels.push_back (c);
    }

    // Lets the engine skip the whole stereo effect stage on a project with none.
    for (const auto& channel : snapshot.channels)
        snapshot.anyEffects = snapshot.anyEffects || channel.effects.anyEnabled();

    for (const auto& track : snapshot.mixerTracks)
        snapshot.anyEffects = snapshot.anyEffects || track.effects.anyEnabled();

    snapshot.anyEffects = snapshot.anyEffects || snapshot.masterEffects.anyEnabled();

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

    // --- automations ---------------------------------------------------------
    // Resolved before clips, since a clip refers to one by index.
    juce::Array<int> automationIds;

    for (const auto& automation : project)
    {
        if (! automation.hasType (ids::AUTOMATION))
            continue;

        if ((int) snapshot.automations.size() >= kMaxAutomations)
        {
            warn ("More than " + juce::String (kMaxAutomations) + " automations; the rest are ignored.");
            break;
        }

        AutomationSnapshot a;
        a.scope = automationScopeFromString (automation[ids::scope].toString());
        a.slotIndex = (int) automation[ids::slot];

        const juce::Identifier property (automation[ids::param].toString());
        a.param = automationParamFromIdentifier (property);

        const auto targetId = (int) automation[ids::targetId];
        juce::String effectType;

        // Resolve the owner to an index, and an effect target to its type, so
        // the audio thread never searches and never compares a string.
        const auto resolveChain = [&] (const juce::ValueTree& owner) -> bool
        {
            int slot = 0;

            for (const auto& effect : owner)
            {
                if (! effect.hasType (ids::EFFECT))
                    continue;

                if (slot++ == a.slotIndex)
                {
                    effectType = effect[ids::type].toString();
                    return true;
                }
            }

            return false;
        };

        bool resolved = false;

        switch (a.scope)
        {
            case AutomationScope::master:
                a.targetIndex = -1;
                resolved = true;
                break;

            case AutomationScope::channel:
            case AutomationScope::channelOsc:
            case AutomationScope::channelEffect:
                for (size_t i = 0; i < snapshot.channels.size(); ++i)
                    if (snapshot.channels[i].id == targetId)
                        a.targetIndex = (int) i;

                resolved = a.targetIndex >= 0;

                // An oscillator slot that is no longer in wavetable mode has
                // nothing to drive, so the clip is dropped rather than left
                // pointing at a parameter the voice will not read.
                if (resolved && a.scope == AutomationScope::channelOsc)
                {
                    const auto& slots = snapshot.channels[(size_t) a.targetIndex].osc;

                    resolved = a.slotIndex >= 0 && a.slotIndex < slots.numSlots
                            && slots.slots[(size_t) a.slotIndex].mode == OscMode::wavetable;
                }

                if (resolved && a.scope == AutomationScope::channelEffect)
                {
                    resolved = false;

                    for (const auto& channel : project)
                        if (channel.hasType (ids::CHANNEL) && (int) channel[ids::id] == targetId)
                            resolved = resolveChain (channel);
                }
                break;

            case AutomationScope::mixerTrack:
            case AutomationScope::mixerEffect:
                for (size_t i = 0; i < snapshot.mixerTracks.size(); ++i)
                    if (snapshot.mixerTracks[i].id == targetId)
                        a.targetIndex = (int) i;

                resolved = a.targetIndex >= 0;

                if (resolved && a.scope == AutomationScope::mixerEffect)
                {
                    resolved = false;

                    for (const auto& track : mixer)
                        if (track.hasType (ids::MIXER_TRACK) && (int) track[ids::id] == targetId)
                            resolved = resolveChain (track);
                }
                break;
        }

        // A target that no longer exists - a deleted channel, or an effect slot
        // that changed type so the parameter no longer applies - is dropped
        // with a warning, exactly like a clip pointing at a missing pattern.
        const auto* spec = resolved ? findParamSpec (a.scope, effectType, property) : nullptr;

        if (spec == nullptr || a.param == AutomationParam::none)
        {
            warn ("Automation \"" + automation[ids::name].toString()
                  + "\" targets something that no longer exists; it is ignored.");
            automationIds.add (-1);
            snapshot.automations.push_back ({});
            continue;
        }

        a.minimum = (float) spec->minimum;
        a.maximum = (float) spec->maximum;
        a.logarithmic = spec->logarithmic;

        if (a.scope == AutomationScope::channelEffect || a.scope == AutomationScope::mixerEffect)
            if (const auto type = effectTypeFor (effectType))
                a.paramIndex = effectParamIndex (*type, property);

        for (const auto& point : automation)
            if (point.hasType (ids::POINT))
                a.points.push_back ({ (double) point[ids::step],
                                      juce::jlimit (0.0f, 1.0f, (float) (double) point[ids::value]),
                                      juce::jlimit (-1.0f, 1.0f, (float) (double) point[ids::curve]) });

        std::stable_sort (a.points.begin(), a.points.end(),
                          [] (const auto& x, const auto& y) { return x.step < y.step; });

        automationIds.add ((int) automation[ids::id]);
        snapshot.automations.push_back (std::move (a));
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
            c.startBar     = juce::jmax (0, (int) clip[ids::startBar]);
            c.lengthBars   = juce::jmax (1, (int) clip[ids::lengthBars]);
            c.trackAudible = trackAudible;

            if (clip[ids::kind].toString() == "automation")
            {
                c.automationIndex = automationIds.indexOf ((int) clip[ids::automationId]);

                if (c.automationIndex < 0)
                {
                    warn ("A clip refers to automation " + clip[ids::automationId].toString()
                          + ", which does not exist; it will do nothing.");
                    continue;
                }

                // A muted track silences its notes; it should silence what its
                // automation does too, or a muted lane still moves the mix.
                if (trackAudible)
                    snapshot.anyAutomation = true;

                snapshot.clips.push_back (c);
                continue;
            }

            if (clip[ids::kind].toString() == "audio")
            {
                const auto channelId = (int) clip[ids::channelId];
                c.channelIndex = snapshot.channelIndexForId (channelId);

                if (c.channelIndex < 0)
                {
                    warn ("A clip refers to channel " + juce::String (channelId)
                          + ", which does not exist; it will not play.");
                    continue;
                }

                snapshot.clips.push_back (c);
                continue;
            }

            c.patternIndex = snapshot.patternIndexForId ((int) clip[ids::patternId]);

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
