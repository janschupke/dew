#include "model/ModuleCatalog.h"

#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

constexpr ParamChoice filterModes[] {
    { "lowpass", "Low pass" },
    { "highpass", "High pass" },
    { "bandpass", "Band pass" },
};

// Ranges reconciled to the widest the engine can actually render, so there is
// one answer rather than one per table. The only effect parameter where the
// four tables disagreed was `cutoff`: the engine loaded it up to 20kHz while
// the automation table and the knob both stopped at 18kHz, which was an
// arbitrary number and cost the filter its top octave.

const ParamSpec commonParams[] {
    { &ids::mix, "Mix", "MIX", "", 0.0, 1.0, 1.0, 0.01, 2 },
};

/** The last mode's index, so the automation range is derived from the table
    rather than being a 2 that has to be remembered when a mode is added. */
constexpr double lastFilterMode = (double) (std::size (filterModes) - 1);

const ParamSpec filterParams[] {
    // A range of 0..lastFilterMode and integral, so an automation curve over it
    // is a curve over the choice INDEX - which is what a choice already is in the
    // slot's parameter block, and what every plugin API makes a discrete
    // parameter. Automatable now: it needed no engine plumbing at all, and it is
    // the proof that a stepped curve and a continuous one are one model.
    { &ids::filterMode, "Mode", "MODE", "", 0.0, lastFilterMode, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::choice, false, /*automatable*/ true, /*integral*/ true, filterModes,
      (int) std::size (filterModes), "lowpass" },
    { &ids::cutoff, "Cutoff", "CUTOFF", " Hz", 20.0, 20000.0, 1200.0, 1.0, 0,
      ParamCurve::logarithmic, ParamControl::field },
    { &ids::resonance, "Resonance", "RES", "", 0.05, 4.0, 0.4, 0.01, 2 },
};

const ParamSpec reverbParams[] {
    { &ids::roomSize, "Size", "SIZE", "", 0.0, 1.0, 0.5, 0.01, 2 },
    { &ids::damping, "Damping", "DAMP", "", 0.0, 1.0, 0.5, 0.01, 2 },
    { &ids::width, "Width", "WIDTH", "", 0.0, 1.0, 1.0, 0.01, 2 },
};

const ParamSpec delayParams[] {
    { &ids::delayMs, "Time", "TIME", " ms", 1.0, 1000.0, 250.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::feedback, "Feedback", "FBK", "", 0.0, 0.95, 0.35, 0.01, 2 },
};

const ParamSpec driveParams[] {
    { &ids::drive, "Drive", "DRIVE", "", 1.0, 40.0, 2.0, 0.1, 1 },
    { &ids::outputGain, "Output", "OUT", "", 0.0, 4.0, 1.0, 0.01, 2 },
};

const ParamSpec chorusParams[] {
    { &ids::rate, "Rate", "RATE", " Hz", 0.01, 20.0, 1.2, 0.01, 2, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::depth, "Depth", "DEPTH", "", 0.0, 1.0, 0.3, 0.01, 2 },
};

const ParamSpec eqParams[] {
    { &ids::lowGainDb, "Low", "LOW", " dB", -24.0, 24.0, 0.0, 0.1, 1, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ true },
    { &ids::midGainDb, "Mid", "MID", " dB", -24.0, 24.0, 0.0, 0.1, 1, ParamCurve::linear,
      ParamControl::knob, true },
    { &ids::midFreq, "Freq", "FREQ", " Hz", 100.0, 8000.0, 900.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::highGainDb, "High", "HIGH", " dB", -24.0, 24.0, 0.0, 0.1, 1, ParamCurve::linear,
      ParamControl::knob, true },
};

} // namespace

const std::vector<EffectDescriptor>& effectDescriptors()
{
    static const std::vector<EffectDescriptor> all {
        { EffectType::filter, "filter", "Filter", filterParams, (int) std::size (filterParams) },
        { EffectType::reverb, "reverb", "Reverb", reverbParams, (int) std::size (reverbParams) },
        { EffectType::delay, "delay", "Delay", delayParams, (int) std::size (delayParams) },
        { EffectType::drive, "drive", "Drive", driveParams, (int) std::size (driveParams) },
        { EffectType::chorus, "chorus", "Chorus", chorusParams, (int) std::size (chorusParams) },
        { EffectType::eq, "eq", "EQ", eqParams, (int) std::size (eqParams) },
    };

    // Not a comment asking someone to keep two things in step: adding an
    // enumerator without a row here stops the program before it starts.
    jassert ((int) all.size() == kNumEffectTypes);

    return all;
}

const EffectDescriptor& effectDescriptor (EffectType type) noexcept
{
    const auto& all = effectDescriptors();
    const auto index = (size_t) type;

    jassert (index < all.size());
    return all[juce::jmin (index, all.size() - 1)];
}

std::optional<EffectType> effectTypeFor (juce::StringRef id)
{
    for (const auto& descriptor : effectDescriptors())
        if (juce::String (descriptor.id) == id)
            return descriptor.type;

    return {};
}

juce::String effectTypeToString (EffectType type)
{
    return effectDescriptor (type).id;
}

juce::String effectTypeDisplayName (EffectType type)
{
    return effectDescriptor (type).displayName;
}

const std::vector<ParamSpec>& commonEffectParams()
{
    static const std::vector<ParamSpec> common { std::begin (commonParams),
                                                 std::end (commonParams) };
    return common;
}

int effectParamIndex (EffectType type, const juce::Identifier& property) noexcept
{
    for (const auto& param : commonEffectParams())
        if (*param.property == property)
            return 0;

    const auto& descriptor = effectDescriptor (type);

    for (int i = 0; i < descriptor.numParams; ++i)
        if (*descriptor.params[i].property == property)
            return kNumCommonEffectParams + i;

    return -1;
}

std::vector<ParamSpec> effectParamsFor (EffectType type)
{
    const auto& descriptor = effectDescriptor (type);

    std::vector<ParamSpec> params { descriptor.params, descriptor.params + descriptor.numParams };

    const auto& common = commonEffectParams();
    params.insert (params.end(), common.begin(), common.end());

    return params;
}

// --- the instrument's own parameters -----------------------------------------

namespace
{

/** A toggle, declared the way every other parameter is.

    ParamControl::toggle existed in the enum and NOTHING used it: every binary
    state in dew - mute, solo, bypass, an oscillator's on/off - lived outside the
    catalog as a raw schema property written directly by a button. That is why
    none of them could be automated, and why a control had no spec to build a
    right-click menu from.

    0..1 and integral, so numDiscreteValues reports two and automationValueFor
    snaps a curve over it to exactly off or on.
*/
constexpr ParamSpec toggleSpec (const juce::Identifier* property, const char* displayName,
                                const char* caption, bool automatable, bool defaultOn = false)
{
    return { property,
             displayName,
             caption,
             "",
             0.0,
             1.0,
             defaultOn ? 1.0 : 0.0,
             1.0,
             0,
             ParamCurve::linear,
             ParamControl::toggle,
             /*bipolar*/ false,
             automatable,
             /*integral*/ true };
}

/** One of a named set, declared the way every other parameter is.

    `defaultIndex` is stated rather than derived from `defaultText` because the
    two answer different questions: the text is what the FILE carries and what
    ParamSpec::defaultVar hands the schema, and the index is where a curve over
    the choice sits. Deriving one from the other would make a typo in either a
    silent disagreement rather than a compile error.
*/
constexpr ParamSpec choiceSpec (const juce::Identifier* property, const char* displayName,
                                const char* caption, const ParamChoice* choices, int numChoices,
                                const char* defaultText, double defaultIndex,
                                bool automatable = false)
{
    return { property,
             displayName,
             caption,
             "",
             0.0,
             (double) (numChoices - 1),
             defaultIndex,
             1.0,
             0,
             ParamCurve::linear,
             ParamControl::choice,
             /*bipolar*/ false,
             automatable,
             /*integral*/ true,
             choices,
             numChoices,
             defaultText };
}

// The oscillator's own named sets. Their ORDER is their index order, so it
// matches the enums in InstrumentType.h - a curve over `wave` is a curve over
// the same number waveformFromString would produce.

constexpr ParamChoice waveforms[] {
    { "sine", "Sine" },
    { "saw", "Saw" },
    { "square", "Square" },
    { "triangle", "Triangle" },
};

constexpr ParamChoice oscModes[] {
    { "classic", "Classic" },
    { "wavetable", "Wavetable" },
};

/** The wavetable bank, by name.

    A second statement of the engine's bank, and unavoidably so: the tables are
    GENERATED in dew_engine and the catalog is in dew_model, which cannot see
    it. A test cross-checks the two, so the duplication is a pair that must
    agree rather than a pair that will quietly drift.
*/
constexpr ParamChoice wavetables[] {
    { "basic", "Basic Shapes" }, { "pulse", "Pulse" }, { "harmonics", "Harmonics" },
    { "formant", "Formant" },    { "fold", "Fold" },
};

constexpr ParamChoice positionSources[] {
    { "envelope", "Envelope" },
    { "lfo", "LFO" },
};

const ParamSpec channelSpecs[] {
    // The ORDER is the schema's, for the reason oscSpecs' is: ProjectSchema
    // splices this block straight into the CHANNEL node between the channel's
    // identity and its `source`.
    //
    // A MIDI note number, so the range is the whole of MIDI and the value is
    // an integer in the file.
    { &ids::basePitch, "Base pitch", "PITCH", "", 0.0, 127.0, 60.0, 1.0, 0, ParamCurve::linear,
      ParamControl::stepper, false, /*automatable*/ false,
      /*integral*/ true },

    { &ids::volume, "Volume", "VOLUME", "", 0.0, 1.0, 0.8, 0.001, 3 },
    { &ids::pan, "Pan", "PAN", "", -1.0, 1.0, 0.0, 0.001, 3, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },

    // Mute is automatable and solo is NOT, and that asymmetry is the point.
    //
    // Mute is a value on one channel: the engine reads it per channel and a
    // curve over it means "silence this, here". Solo is a RELATION between
    // channels - anyChannelSolo is a snapshot-wide precomputation, and honouring
    // a curve over it would mean re-deciding every channel's audibility every
    // block. Mute already expresses everything a curve wants from either.
    toggleSpec (&ids::muted, "Mute", "MUTE", /*automatable*/ true),
    toggleSpec (&ids::solo, "Solo", "SOLO", /*automatable*/ false),
};

const ParamSpec ampSpecs[] {
    // Ten seconds, which is what the engine renders. The knobs stopped at two
    // and four, so the top of the envelope was simply unreachable.
    //
    // Logarithmic, because linear puts every usable attack in the first one per
    // cent of the travel: half a millisecond to ten seconds is four and a half
    // decades, and a knob that spends nine tenths of its sweep between five and
    // ten seconds is a knob with one useful position.
    { &ids::attack, "Attack", "ATTACK", " s", 0.0005, 10.0, 0.005, 0.0005, 4,
      ParamCurve::logarithmic },
    { &ids::decay, "Decay", "DECAY", " s", 0.0005, 10.0, 0.120, 0.0005, 4,
      ParamCurve::logarithmic },
    { &ids::sustain, "Sustain", "SUSTAIN", "", 0.0, 1.0, 0.700, 0.001, 3 },
    { &ids::release, "Release", "RELEASE", " s", 0.002, 10.0, 0.150, 0.001, 3,
      ParamCurve::logarithmic },
};

const ParamSpec oscSpecs[] {
    // The ORDER is the schema's, and load-bearing: ProjectSchema generates the
    // OSC node from this table, ValueTree property order follows it, and the
    // committed examples are byte-compared against what the factory writes. A
    // reshuffle here is a rewrite of every .dew in the repository.
    //
    // `enabled` is a parameter rather than a hand-written schema property, and
    // that is the opposite of an EFFECT's `enabled`: a slot being on is part of
    // the PATCH - a pad is three oscillators and a sub bass is one - where an
    // effect's is a bypass somebody flicks while mixing.
    toggleSpec (&ids::enabled, "Enabled", "ON", /*automatable*/ false, /*defaultOn*/ true),

    choiceSpec (&ids::wave, "Wave", "WAVE", waveforms, (int) std::size (waveforms), "saw", 1.0),

    // Four octaves either way, which is what the engine renders; the stepper
    // offered three.
    { &ids::octave, "Octave", "OCT", "", -4.0, 4.0, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::stepper, true, /*automatable*/ false,
      /*integral*/ true },

    // One semitone either way, and here the KNOB is the one that wins. The
    // engine clamps at twelve semitones, but that is what `octave` is for, and
    // a control covering two octaves cannot be nudged by a cent - which is the
    // only thing anyone detunes an oscillator by.
    //
    // NOT integral, and the schema is why: `detuneCents` is declared a double
    // there and every file has carried one, so an int default would flip the
    // type the schema coerces to and quietly round a stored 12.5 to 12. It
    // would also make numDiscreteValues report two hundred and one steps for a
    // parameter that is continuous.
    { &ids::detuneCents, "Detune", "DETUNE", " c", -100.0, 100.0, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ true, /*automatable*/ false },

    { &ids::gain, "Gain", "GAIN", "", 0.0, 1.0, 0.8, 0.01, 2 },

    choiceSpec (&ids::mode, "Mode", "MODE", oscModes, (int) std::size (oscModes), "classic", 0.0),
    choiceSpec (&ids::wavetable, "Table", "TABLE", wavetables, (int) std::size (wavetables),
                "basic", 0.0),

    { &ids::wavePosition, "Position", "POSITION", "", 0.0, 1.0, 0.0, 0.01, 2 },
    { &ids::wavePositionMod, "Mod", "MOD", "", -1.0, 1.0, 0.0, 0.01, 2, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ true },

    choiceSpec (&ids::wavePositionSource, "Source", "SOURCE", positionSources,
                (int) std::size (positionSources), "envelope", 0.0),

    { &ids::wavePositionRate, "Rate", "RATE", " Hz", 0.01, 20.0, 1.0, 0.01, 2,
      ParamCurve::logarithmic },

    { &ids::unisonVoices, "Unison", "UNISON", "", 1.0, (double) kMaxUnisonVoices, 1.0, 1.0, 0,
      ParamCurve::linear, ParamControl::knob, false, /*automatable*/ false,
      /*integral*/ true },
    { &ids::unisonDetune, "Spread", "SPREAD", " c", 0.0, 50.0, 0.0, 0.5, 1 },
};

const ParamSpec mixerTrackSpecs[] {
    // The three-way disagreement the plan named: the fader offered 0..1.5, the
    // engine clamped at 2.0 and automation mapped onto 0..1, so automating a
    // fader reached two thirds of its travel and the top third of the engine's
    // range was unreachable from anywhere. The FADER wins: 2.0 is six decibels
    // no control ever offered.
    { &ids::gain, "Gain", "GAIN", "", 0.0, 1.5, 0.8, 0.001, 3 },
    { &ids::pan, "Pan", "PAN", "", -1.0, 1.0, 0.0, 0.001, 3, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },

    // A track says `mute` where a channel says `muted`. Two spellings of one
    // idea, kept because both are already in every saved file.
    toggleSpec (&ids::mute, "Mute", "MUTE", /*automatable*/ true),
    toggleSpec (&ids::solo, "Solo", "SOLO", /*automatable*/ false),
};

/** A sample's own settings.

    None of them automatable, and each for its own reason. Reversing a sample is
    a discontinuity in a read pointer rather than a parameter change - there is
    no meaning to doing it halfway through a note that is already sounding - and
    a fade or a transpose is set once for a take rather than moved through it.

    Declared anyway, because a spec is what gives a control its range, its
    decimals and its "reset to default", and these three knobs were the last in
    the application still stating their own range by hand.
*/
const ParamSpec sampleSpecs[] {
    { &ids::fadeInMs, "Fade in", "FADE IN", " ms", 0.0, 2000.0, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ false, /*automatable*/ false },
    { &ids::fadeOutMs, "Fade out", "FADE OUT", " ms", 0.0, 2000.0, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ false, /*automatable*/ false },
    // A double in the schema, so not integral here - see detuneCents.
    { &ids::transpose, "Pitch", "PITCH", "", -24.0, 24.0, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ true, /*automatable*/ false },

    toggleSpec (&ids::reverse, "Reverse", "REV", /*automatable*/ false),
    toggleSpec (&ids::loop, "Loop", "LOOP", /*automatable*/ false),
};

/** A soundfont channel's own settings.

    Every one of them is an OFFSET, not a value, and that is the whole design.
    An SF2 preset colours an instrument it does not own by adding to the
    instrument's generators rather than replacing them; these knobs are the same
    mechanism with a person on the other end, so the font stays authoritative and
    a channel bends it. A knob that SET a cutoff would silently discard whatever
    the font's author chose, per region, and there is no honest value to show
    when a preset spans forty regions that disagree.

    `transpose` is deliberately the same identifier the sample table uses. It
    means the same thing, in the same units, over the same range - two spellings
    of one idea is exactly what the rest of this file exists to stop.

    None of them automatable yet: a curve needs a scope to point at, and a
    SOUNDFONT node has none. The times could not have one anyway - an envelope's
    stages are latched when a note starts, so a curve moving them halfway through
    a note that is already sounding has nothing to mean.
*/
const ParamSpec soundFontSpecs[] {
    // A double in the schema, so not integral here - see detuneCents.
    { &ids::transpose, "Pitch", "PITCH", "", -24.0, 24.0, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ true, /*automatable*/ false },

    { &ids::tuneCents, "Tune", "TUNE", " c", -100.0, 100.0, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ true, /*automatable*/ false },

    // Linear, and that is not an oversight: a cent IS a logarithmic unit, so
    // this offset is already in the domain ParamCurve::logarithmic exists to
    // reach. That curve could not be used here anyway - it maps min*(max/min)^v,
    // which needs a positive minimum, and half of this range is below zero.
    { &ids::filterOffset, "Filter", "FILTER", " c", -2400.0, 2400.0, 0.0, 10.0, 0,
      ParamCurve::linear, ParamControl::knob, /*bipolar*/ true, /*automatable*/ false },

    // Multipliers rather than times, because that is what an offset in timecents
    // IS: the format stores envelope stages logarithmically, so adding to one
    // scales it. A quarter to four times, with 1 in the middle of the travel.
    { &ids::attackScale, "Attack", "ATTACK", "x", 0.25, 4.0, 1.0, 0.01, 2, ParamCurve::logarithmic,
      ParamControl::knob, /*bipolar*/ false, /*automatable*/ false },
    { &ids::releaseScale, "Release", "RELEASE", "x", 0.25, 4.0, 1.0, 0.01, 2,
      ParamCurve::logarithmic, ParamControl::knob, /*bipolar*/ false, /*automatable*/ false },

    // How much of the format's velocity-to-attenuation curve is applied. 1 is
    // what the SF2 specification says; 0 plays every note at full level, which
    // is what a stepped pattern usually wants.
    { &ids::velocitySens, "Velocity", "VEL", "", 0.0, 1.0, 1.0, 0.01, 2, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ false, /*automatable*/ false },
};

/** The arrangement's own parameters. One: the tempo.

    20..999 reconciles the FOURTH disagreement of the same kind the rest of this
    file exists to end - the transport's field offered 20..300 while
    buildSnapshot clamped to 20..999, so the top two thirds of what the engine
    renders could not be typed in.

    Logarithmic for the same reason cutoff is: doubling a tempo is an octave.
    Linearly the midpoint of 20..999 is 510bpm; logarithmically it is 141, which
    is the middle of what anyone plays.
*/
const ParamSpec projectSpecs[] {
    { &ids::tempoBpm, "Tempo", "TEMPO", " bpm", 20.0, 999.0, 128.0, 0.1, 1, ParamCurve::logarithmic,
      ParamControl::field },
};

/** An instrument's parameters, as the nodes they actually live on.

    An effect is one node with a flat list and does not have to say so; an
    instrument is a channel, three oscillator slots and an envelope. That is the
    whole of the asymmetry between the two descriptors, and naming it beats
    flattening: the tree here is the tree the schema builds and the editor
    points at.

    The CHANNEL group is NOT a preset's. Volume, pan and base pitch are the
    instrument's parameters - the engine reads them, automation drives them, and
    a plugin wrapper would expose them - but they are not the SOUND. A preset
    that set the volume would be a level jump in the middle of a mix, and one
    that set base pitch would retune a part that is already written.
*/
const ParamGroup synthGroups[] {
    { &ids::CHANNEL, "", "Channel", channelSpecs, (int) std::size (channelSpecs), 1,
      /*inPreset*/ false },
    { &ids::OSC, "oscillators", "Oscillator", oscSpecs, (int) std::size (oscSpecs),
      kMaxOscillators },
    { &ids::AMP, "amp", "Envelope", ampSpecs, (int) std::size (ampSpecs) },
};

/** The audio channel's. Thin, and honestly so: everything that makes one
    recording differ from another is the recording, and the parameters are the
    handful of things done TO it. The file itself is deliberately not among
    them - a preset carrying a path points at somebody else's disk, and a trim
    measured in frames means nothing against another take. */
const ParamGroup audioGroups[] {
    { &ids::CHANNEL, "", "Channel", channelSpecs, (int) std::size (channelSpecs), 1,
      /*inPreset*/ false },
    { &ids::SAMPLE, "sample", "Sample", sampleSpecs, (int) std::size (sampleSpecs) },
};

/** The soundfont channel's. Thin for the same reason the audio one is: what
    makes one soundfont differ from another is the file, and these are the
    handful of things done TO it.

    The file, the bank and the program are deliberately NOT here, so they do not
    travel in a preset - the same line the audio instrument draws. A preset
    carrying a path points at somebody else's disk, and a bank and program mean
    nothing against another font. */
const ParamGroup soundFontGroups[] {
    { &ids::CHANNEL, "", "Channel", channelSpecs, (int) std::size (channelSpecs), 1,
      /*inPreset*/ false },
    { &ids::SOUNDFONT, "soundfont", "SoundFont", soundFontSpecs, (int) std::size (soundFontSpecs) },
};

/** One declared table, as the vector the accessors hand out.

    Returns BY VALUE, and the static that caches it belongs to the accessor
    rather than to this helper. It used to hold the static itself:

        template <size_t N>
        const std::vector<ParamSpec>& asVector (const ParamSpec (&table)[N])
        {
            static const std::vector<ParamSpec> specs { ... };   // keyed on N
        }

    A function template is instantiated once per N, so that one static was
    shared by every table of the same LENGTH, and the first caller decided what
    all of them returned. channelSpecs and sampleSpecs are both five rows, and
    ampSpecs and mixerTrackSpecs are both four, so sampleParamSpecs() handed
    back the channel's volume and pan, and whichever of the envelope and the
    fader was asked for second returned the other one's ranges. Nothing said so:
    the lookups that then failed end in a jassert, which a RelWithDebInfo build
    compiles out and leaves as a knob that silently spans nought to one.
*/
template <size_t N> std::vector<ParamSpec> toVector (const ParamSpec (&table)[N])
{
    return { std::begin (table), std::end (table) };
}

} // namespace

#define DEW_PARAM_TABLE(accessor, table)                                                           \
    const std::vector<ParamSpec>& accessor()                                                       \
    {                                                                                              \
        static const std::vector<ParamSpec> specs = toVector (table);                              \
        return specs;                                                                              \
    }

DEW_PARAM_TABLE (channelParamSpecs, channelSpecs)
DEW_PARAM_TABLE (ampParamSpecs, ampSpecs)
DEW_PARAM_TABLE (oscParamSpecs, oscSpecs)
DEW_PARAM_TABLE (mixerTrackParamSpecs, mixerTrackSpecs)
DEW_PARAM_TABLE (sampleParamSpecs, sampleSpecs)
DEW_PARAM_TABLE (soundFontParamSpecs, soundFontSpecs)
DEW_PARAM_TABLE (projectParamSpecs, projectSpecs)

#undef DEW_PARAM_TABLE

const std::vector<InstrumentDescriptor>& instrumentDescriptors()
{
    static const std::vector<InstrumentDescriptor> all {
        { InstrumentType::synth, "synth", "Synth", synthGroups, (int) std::size (synthGroups) },
        { InstrumentType::audio, "audio", "Audio", audioGroups, (int) std::size (audioGroups) },
        { InstrumentType::soundfont, "soundfont", "SoundFont", soundFontGroups,
          (int) std::size (soundFontGroups) },
    };

    // The same assertion effectDescriptors() makes, and for the same reason:
    // adding an enumerator without a row here stops the program before it
    // starts rather than leaving a kind of channel with no parameters.
    jassert ((int) all.size() == kNumInstrumentTypes);

    return all;
}

const InstrumentDescriptor& instrumentDescriptor (InstrumentType type) noexcept
{
    const auto& all = instrumentDescriptors();
    const auto index = (size_t) type;

    jassert (index < all.size());
    return all[juce::jmin (index, all.size() - 1)];
}

std::optional<InstrumentType> instrumentTypeFor (juce::StringRef id)
{
    for (const auto& descriptor : instrumentDescriptors())
        if (juce::String (descriptor.id) == id)
            return descriptor.type;

    return {};
}

juce::String instrumentTypeToString (InstrumentType type)
{
    return instrumentDescriptor (type).id;
}

juce::String instrumentTypeDisplayName (InstrumentType type)
{
    return instrumentDescriptor (type).displayName;
}

ParamGroup effectGroup (EffectType type) noexcept
{
    // An effect is the degenerate instrument: one node, every parameter on it.
    // Saying so here is what lets the state functions walk both descriptors
    // with one loop body rather than two that must agree.
    const auto& descriptor = effectDescriptor (type);

    return { &ids::EFFECT, "effects", descriptor.displayName, descriptor.params,
             descriptor.numParams };
}

const ParamSpec* instrumentParamSpec (const juce::Identifier& property) noexcept
{
    // Order matters where two tables name the same property. `gain` is both an
    // oscillator's level and a mixer track's fader, and `pan` is both a
    // channel's and a track's; the instrument tables are searched first
    // because this is the INSTRUMENT lookup, and the mixer asks for its own.
    for (const auto* table : { &channelParamSpecs(), &ampParamSpecs(), &oscParamSpecs(),
                               &sampleParamSpecs(), &soundFontParamSpecs() })
        for (const auto& spec : *table)
            if (*spec.property == property)
                return &spec;

    return nullptr;
}

const ParamSpec* mixerTrackParamSpec (const juce::Identifier& property) noexcept
{
    for (const auto& spec : mixerTrackParamSpecs())
        if (*spec.property == property)
            return &spec;

    return nullptr;
}

const ParamSpec& requireMixerTrackParamSpec (const juce::Identifier& property)
{
    const auto* spec = mixerTrackParamSpec (property);
    jassert (spec != nullptr);

    static const ParamSpec fallback {};
    return spec != nullptr ? *spec : fallback;
}

const ParamSpec& requireInstrumentParamSpec (const juce::Identifier& property)
{
    const auto* spec = instrumentParamSpec (property);

    // A caller asking for a spec that does not exist has a bug in the table,
    // not in the document, and a default-shaped fallback would hide it behind a
    // knob that silently spans nought to one.
    jassert (spec != nullptr);

    static const ParamSpec fallback {};
    return spec != nullptr ? *spec : fallback;
}

} // namespace dew
