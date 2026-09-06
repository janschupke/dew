// =============================================================================
// The readers declared in SnapshotReaders.h.
//
// Split out of EngineSnapshot.cpp, which was 929 lines of which 400 were
// buildSnapshot and 390 were these. They share nothing with the passes above
// them but the types they answer in, so this is a file about reading a node
// and that one is a file about assembling a snapshot from what they read.
// =============================================================================

#include "engine/SnapshotReaders.h"

#include "engine/SampleProvider.h"
#include "engine/Wavetable.h"

#include <cmath>

#include "model/AssetPaths.h"
#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"

namespace dew::snapshotRead
{

/** A property read from a node and clamped by what the catalog declares it to
    be, with the declared default standing in for a missing one.

    The default matters as much as the clamp: a node with no AMP child used to
    read sustain as zero, which is a silent note, and a slot with no `mix` read
    as zero, which is a bypassed effect. Both were silent failures of a missing
    property rather than of a wrong value.
*/
float clampBySpec (const juce::Identifier& property, const juce::ValueTree& node)
{
    const auto& spec = requireInstrumentParamSpec (property);

    return spec.clamp ((float) (double) node.getProperty (property, spec.defaultVar()));
}

namespace
{

/** A synced LFO's rate, in Hz.

    `divisionIndex` indexes ModuleCatalog's noteDivisions, whose rows ARE the
    note values 1/1, 1/2, 1/4 ... - so the value is 1 / (1 << index) of a whole
    note and no second table has to say so.

    A whole note lasts `beatUnit` beats, because a beat IS a note of value
    1/beatUnit; a beat lasts 60/bpm seconds, which is the one thing Transport
    and this have to agree on. So the rate is (bpm/60) * (1 << index) /
    beatUnit - two per second for a quarter at 120bpm in 4/4.

    This is the one place beatUnit reaches the engine, and it reaches it on the
    MESSAGE thread. It must never get near Transport::samplesPerStepFor: a metre
    is not a tempo, and folding beatUnit into a step duration is the mistake
    Meter.h exists to prevent.
*/
float syncedLfoHz (double tempoBpm, int beatUnit, int divisionIndex) noexcept
{
    const auto beatsPerSecond = juce::jmax (1.0, tempoBpm) / 60.0;
    const auto notesPerWhole = (double) (1 << juce::jlimit (0, 5, divisionIndex));

    return (float) (beatsPerSecond * notesPerWhole / (double) juce::jmax (1, beatUnit));
}

/** Which row of a choice parameter a node holds, by the ids the catalog
    declares. -1 becomes the declared default's index rather than 0, so a file
    naming a division this build does not have takes what the catalog says
    instead of the longest one. */
int choiceIndexOf (const juce::Identifier& property, const juce::ValueTree& node)
{
    const auto& spec = requireInstrumentParamSpec (property);
    const auto stored = node.getProperty (property, spec.defaultVar()).toString();

    for (int i = 0; i < spec.numChoices; ++i)
        if (stored == spec.choices[i].id)
            return i;

    return (int) spec.defaultValue;
}

} // namespace

OscBankSnapshot readOscBank (const juce::ValueTree& instrument, const juce::String& ownerName,
                             double tempoBpm, int beatUnit,
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
        s.enabled = (bool) osc.getProperty (ids::enabled, true);
        s.mode = oscModeFromString (osc[ids::mode].toString());

        // Each generator's parameters live on its own node under the slot. The
        // node a property lives on is the MODEL's answer - generatorNodeFor -
        // so this reader does not carry its own copy of which is whose.
        const auto classic = generatorNodeFor (osc, ids::wave);
        const auto wavetable = generatorNodeFor (osc, ids::wavePosition);

        s.wave = waveformFromString (classic[ids::wave].toString());
        // Clamped by the declared spec rather than by a number written here.
        // These used to be a second opinion about the range, and the knobs were
        // a third: the octave stepper offered three when the engine renders
        // four.
        s.octave = (int) clampBySpec (ids::octave, osc);
        s.detuneCents = clampBySpec (ids::detuneCents, osc);
        s.gain = clampBySpec (ids::gain, osc);

        const auto tableName = wavetable[ids::wavetable].toString();
        const auto tableIndex = wavetableIndexFor (tableName);

        // A name this build does not know is a fault in the FILE, not a
        // different sound: say so rather than quietly play something else.
        if (tableIndex < 0 && s.mode == OscMode::wavetable)
            warn (ownerName + " asks for wavetable \"" + tableName
                  + "\", which this build does not have; using the first one.");

        s.table = juce::jmax (0, tableIndex);
        s.position = clampBySpec (ids::wavePosition, wavetable);
        s.positionMod = clampBySpec (ids::wavePositionMod, wavetable);
        s.positionSource = positionSourceFromString (wavetable[ids::wavePositionSource].toString());
        s.positionRate = clampBySpec (ids::wavePositionRate, wavetable);
        s.unisonVoices = (int) clampBySpec (ids::unisonVoices, wavetable);
        s.unisonDetune = clampBySpec (ids::unisonDetune, wavetable);

        // The slot's LFO, on its own node beside the two generators' - and read
        // whichever generator the slot runs, because it is the SLOT's.
        const auto lfo = generatorNodeFor (osc, ids::lfoOn);

        s.lfoOn = (bool) lfo.getProperty (ids::lfoOn, false);
        s.lfoSync = (bool) lfo.getProperty (ids::lfoSync, false);
        s.lfoWave = waveformFromString (lfo[ids::lfoWave].toString());
        s.lfoToPitch = clampBySpec (ids::lfoToPitch, lfo);
        s.lfoToVolume = clampBySpec (ids::lfoToVolume, lfo);
        s.lfoToPan = clampBySpec (ids::lfoToPan, lfo);

        // Resolved HERE, on the message thread, the way pitchRatio is - see
        // OscSettings::lfoHz for why the render path must not work it out from
        // the transport instead.
        s.lfoHz = s.lfoSync
                      ? syncedLfoHz (tempoBpm, beatUnit, choiceIndexOf (ids::lfoDivision, lfo))
                      : clampBySpec (ids::lfoRate, lfo);

        // On is not the same as moving. A slot whose LFO is switched on with
        // every depth at zero renders through the plain pass, which is what
        // keeps it bit-identical to a slot that has no LFO at all.
        s.lfoActive = s.lfoOn
                      && ! (juce::exactlyEqual (s.lfoToPitch, 0.0f)
                            && juce::exactlyEqual (s.lfoToVolume, 0.0f)
                            && juce::exactlyEqual (s.lfoToPan, 0.0f));

        // The slot's row of the FM matrix, on the slot itself beside `gain`.
        s.fmTo[0] = clampBySpec (ids::fmTo1, osc);
        s.fmTo[1] = clampBySpec (ids::fmTo2, osc);
        s.fmTo[2] = clampBySpec (ids::fmTo3, osc);
        s.fmOut = clampBySpec (ids::fmOut, osc);

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

    // Decided ONCE here rather than by the audio thread every block.
    bank.anyFm = anyFmIn (bank);

    return bank;
}

bool anyFmIn (const OscBankSnapshot& bank) noexcept
{
    for (int src = 0; src < bank.numSlots; ++src)
    {
        const auto& row = bank.slots[(size_t) src];

        if (! row.enabled)
            continue;

        // An output that is not exactly full needs the FM path too: that is the
        // path which applies the output column at all.
        if (! juce::exactlyEqual (row.fmOut, 1.0f))
            return true;

        for (int dst = 0; dst < bank.numSlots; ++dst)
            if (bank.slots[(size_t) dst].enabled
                && ! juce::exactlyEqual (row.fmTo[(size_t) dst], 0.0f))
                return true;
    }

    return false;
}

AmpSettings readAmp (const juce::ValueTree& amp)
{
    AmpSettings s;

    // A zero attack clicks and a zero release cuts abruptly, so the declared
    // minimums are short rather than zero - and they are declared, in the same
    // table the envelope's knobs are built from.
    s.attack = clampBySpec (ids::attack, amp);
    s.decay = clampBySpec (ids::decay, amp);
    s.sustain = clampBySpec (ids::sustain, amp);
    s.release = clampBySpec (ids::release, amp);
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
AutomationParam automationParamFromIdentifier (AutomationScope scope,
                                               const juce::Identifier& property)
{
    // The one spelling that names two parameters. An oscillator slot's on/off
    // and an effect slot's bypass are different things on different nodes, and
    // only the scope can say which was addressed - see the header.
    if (property == ids::enabled)
        return scope == AutomationScope::channelOsc ? AutomationParam::oscEnabled
                                                    : AutomationParam::enabled;

    static const std::pair<const juce::Identifier*, AutomationParam> table[] {
        { &ids::volume, AutomationParam::volume },
        { &ids::pan, AutomationParam::pan },
        { &ids::gain, AutomationParam::gain },
        { &ids::cutoff, AutomationParam::cutoff },
        { &ids::wavePosition, AutomationParam::position },
        { &ids::octave, AutomationParam::oscOctave },
        { &ids::detuneCents, AutomationParam::oscDetuneCents },
        { &ids::wavePositionMod, AutomationParam::positionMod },
        { &ids::wavePositionRate, AutomationParam::positionRate },
        { &ids::unisonDetune, AutomationParam::unisonDetune },
        { &ids::lfoRate, AutomationParam::lfoRate },
        { &ids::lfoToPitch, AutomationParam::lfoToPitch },
        { &ids::lfoToVolume, AutomationParam::lfoToVolume },
        { &ids::lfoToPan, AutomationParam::lfoToPan },
        { &ids::fmTo1, AutomationParam::fmTo1 },
        { &ids::fmTo2, AutomationParam::fmTo2 },
        { &ids::fmTo3, AutomationParam::fmTo3 },
        { &ids::fmOut, AutomationParam::fmOut },
        { &ids::resonance, AutomationParam::resonance },
        { &ids::mix, AutomationParam::mix },
        { &ids::roomSize, AutomationParam::roomSize },
        { &ids::damping, AutomationParam::damping },
        { &ids::width, AutomationParam::width },
        { &ids::delayMs, AutomationParam::delayMs },
        { &ids::feedback, AutomationParam::feedback },
        { &ids::drive, AutomationParam::drive },
        { &ids::outputGain, AutomationParam::outputGain },
        { &ids::rate, AutomationParam::rate },
        { &ids::depth, AutomationParam::depth },
        { &ids::lowGainDb, AutomationParam::lowGainDb },
        { &ids::midGainDb, AutomationParam::midGainDb },
        { &ids::midFreq, AutomationParam::midFreq },
        { &ids::highGainDb, AutomationParam::highGainDb },
        { &ids::tone, AutomationParam::tone },
        { &ids::centreFreq, AutomationParam::centreFreq },
        { &ids::threshold, AutomationParam::threshold },
        { &ids::ratio, AutomationParam::ratio },
        { &ids::attackMs, AutomationParam::attackMs },
        { &ids::releaseMs, AutomationParam::releaseMs },
        { &ids::makeup, AutomationParam::makeup },
        { &ids::ceiling, AutomationParam::ceiling },

        // The discrete ones. `mute` and `muted` are two spellings of one idea -
        // a mixer track says mute and a channel says muted - and both resolve
        // here, because the scope already says which node is being addressed.
        // The amplitude envelope, and a soundfont channel's offsets into the
        // font it plays. Both are latched at note-on, so a curve over one moves
        // the next note rather than bending the one already sounding.
        { &ids::attack, AutomationParam::ampAttack },
        { &ids::decay, AutomationParam::ampDecay },
        { &ids::sustain, AutomationParam::ampSustain },
        { &ids::release, AutomationParam::ampRelease },

        { &ids::transpose, AutomationParam::sfTranspose },
        { &ids::tuneCents, AutomationParam::sfTuneCents },
        { &ids::filterOffset, AutomationParam::sfFilterOffset },
        { &ids::attackScale, AutomationParam::sfAttackScale },
        { &ids::releaseScale, AutomationParam::sfReleaseScale },
        { &ids::velocitySens, AutomationParam::sfVelocitySens },

        { &ids::filterMode, AutomationParam::filterMode },
        { &ids::distortionMode, AutomationParam::distortionMode },
        { &ids::mute, AutomationParam::muted },
        { &ids::muted, AutomationParam::muted },
        { &ids::tempoBpm, AutomationParam::tempoBpm },
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
        block[(size_t) index] = spec.clamp (
            (float) (double) effect.getProperty (*spec.property, spec.defaultVar()));
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
            warn ("More than " + juce::String (kMaxEffectUnits) + " effects in the project; "
                  + ownerName + " is not fully rendered.");
            break;
        }

        chain.slots[(size_t) chain.numSlots++] = slot;
    }

    return chain;
}

/** Fills in an audio channel's sample settings, and fetches its audio.

    Every value is clamped against the audio that was actually found rather than
    against what the document claims, so a trim left over from a longer take
    cannot make the render path read off the end of a shorter one.
*/
/** Fills in a soundfont channel's offsets, and fetches its font.

    The knobs are read as OFFSETS - the SF2 mechanism for colouring an
    instrument you do not own - so nothing here has to be clamped against the
    font. What it plays is decided per region when a note starts, against
    values the loader already validated against the sample pool.
*/
void readSoundFont (ChannelSnapshot& c, const juce::ValueTree& channel,
                    SoundFontProvider* soundFonts,
                    const std::function<void (const juce::String&)>& warn)
{
    const auto node = channel.getChildWithName (ids::SOUNDFONT);

    if (! node.isValid())
        return;

    auto& settings = c.soundFontSettings;

    settings.bank = juce::jlimit (0, 128, (int) node[ids::bank]);
    settings.program = juce::jlimit (0, 127, (int) node[ids::program]);

    // clampBySpec, not a local clamp of the same two numbers: this reader had
    // its own, and it dropped the declared DEFAULT. A node missing attackScale
    // read the minimum, 0.25x, where the catalog says 1x, and one missing
    // velocitySens read 0 - a soundfont that ignores how hard a key is struck.
    settings.transposeSemitones = clampBySpec (ids::transpose, node);
    settings.tuneCents = clampBySpec (ids::tuneCents, node);
    settings.filterOffsetCents = clampBySpec (ids::filterOffset, node);
    settings.attackScale = clampBySpec (ids::attackScale, node);
    settings.releaseScale = clampBySpec (ids::releaseScale, node);
    settings.velocitySensitivity = clampBySpec (ids::velocitySens, node);

    const auto path = node[ids::file].toString();

    if (path.isEmpty() || soundFonts == nullptr)
        return;

    auto font = soundFonts->soundFontFor (path);

    if (font == nullptr)
    {
        warn ("Channel \"" + channel[ids::name].toString() + "\" refers to soundfont \"" + path
              + "\", which could not be read; it will not play.");
        return;
    }

    if (font->presetFor (settings.bank, settings.program) == nullptr)
    {
        warn ("Channel \"" + channel[ids::name].toString() + "\" asks for \""
              + node[ids::presetName].toString() + "\", which \"" + path
              + "\" does not contain; it will not play.");
        return;
    }

    c.soundFont = font;
}

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
    { return (int) juce::jmax (0.0, ms * 0.001 * sourceSampleRate); };

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
} // namespace dew::snapshotRead
