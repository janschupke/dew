#include "model/ModuleCatalog.h"

#include "model/GeneratorCatalog.h"

#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

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
constexpr ParamSpec toggleSpec (const juce::Identifier* property, bool automatable,
                                bool defaultOn = false)
{
    return { property,
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
constexpr ParamSpec choiceSpec (const juce::Identifier* property, const ParamChoice* choices,
                                int numChoices, const char* defaultText, double defaultIndex,
                                bool automatable = false)
{
    return { property,
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
    { "sine", StringId::choice_wave_sine },
    { "saw", StringId::choice_wave_saw },
    { "square", StringId::choice_wave_square },
    { "triangle", StringId::choice_wave_triangle },
};

constexpr ParamChoice oscModes[] {
    { "classic", StringId::choice_oscMode_classic },
    { "wavetable", StringId::choice_oscMode_wavetable },
};

/** The wavetable bank, by name.

    A second statement of the engine's bank, and unavoidably so: the tables are
    GENERATED in dew_engine and the catalog is in dew_model, which cannot see
    it. A test cross-checks the two, so the duplication is a pair that must
    agree rather than a pair that will quietly drift.
*/
constexpr ParamChoice wavetables[] {
    { "basic", StringId::choice_wavetable_basic },
    { "pulse", StringId::choice_wavetable_pulse },
    { "harmonics", StringId::choice_wavetable_harmonics },
    { "formant", StringId::choice_wavetable_formant },
    { "fold", StringId::choice_wavetable_fold },
};

constexpr ParamChoice positionSources[] {
    { "envelope", StringId::choice_positionSource_envelope },
    { "lfo", StringId::choice_positionSource_lfo },
};

/** How often a synced LFO comes round, as a fraction of a WHOLE NOTE.

    The ids are words rather than "1/4", and that is forced: an i18n key segment
    must be lowerCamel, and cmake/GenStrings.cmake makes a slash or a leading
    digit a FATAL_ERROR at configure time rather than a warning. The DISPLAY
    text is "1/4" - see choice.lfoDivision in en.json.

    The order is load-bearing twice over: an index into it is what a curve over
    the choice would snap to, and the note value IS 1 / (1 << index) of a whole
    note - which is why nothing anywhere holds a second table saying what each
    row means. See syncedLfoHz in SnapshotReaders.
*/
constexpr ParamChoice noteDivisions[] {
    { "whole", StringId::choice_lfoDivision_whole },
    { "half", StringId::choice_lfoDivision_half },
    { "quarter", StringId::choice_lfoDivision_quarter },
    { "eighth", StringId::choice_lfoDivision_eighth },
    { "sixteenth", StringId::choice_lfoDivision_sixteenth },
    { "thirtySecond", StringId::choice_lfoDivision_thirtySecond },
};

const ParamSpec channelSpecs[] {
    // The ORDER is the schema's, for the reason oscSpecs' is: ProjectSchema
    // splices this block straight into the CHANNEL node between the channel's
    // identity and its `source`.
    //
    // A MIDI note number, so the range is the whole of MIDI and the value is
    // an integer in the file.
    { &ids::basePitch, "", 0.0, 127.0, 60.0, 1.0, 0, ParamCurve::linear, ParamControl::stepper,
      false, /*automatable*/ false,
      /*integral*/ true },

    { &ids::volume, "", 0.0, 1.0, 0.8, 0.001, 3 },
    { &ids::pan, "", -1.0, 1.0, 0.0, 0.001, 3, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },

    // Whether the channel plays, and the only such flag there is.
    //
    // There was a solo beside it, deliberately NOT automatable: mute is a value
    // on one channel and solo is a RELATION between channels, so a curve over
    // solo would mean re-deciding every channel's audibility every block. What
    // ended that argument was the control rather than the curve - one state per
    // track, on for plays and off for does not, with shift-click to say it of
    // every track at once. Two indicators for one question is what made the
    // relation necessary; without it, mute says everything either said.
    toggleSpec (&ids::muted, /*automatable*/ true),
};

/** The amplitude envelope, which only an oscillator channel has.

    Every one of them is automatable, and that flag was dead until there was a
    channelAmp scope for a curve to be addressed to. What a curve MEANS here is
    the same thing it means for an oscillator's gain: a voice latches its
    envelope at note-on, so a curve moves the NEXT note rather than bending the
    one already sounding. See SynthVoice's own comment for why that is the rule
    and wavetable position is the single exception to it.
*/
const ParamSpec ampSpecs[] {
    // Ten seconds, which is what the engine renders. The knobs stopped at two
    // and four, so the top of the envelope was simply unreachable.
    //
    // Cubic and starting at ZERO - see ParamCurve::cubic. These were
    // logarithmic, which is right for a frequency and wrong for a time: a
    // logarithm cannot reach zero, so the range began at half a millisecond and
    // then had four and a half decades to cross, and half the knob's travel was
    // spent below seventy milliseconds. An attack of nothing is a setting
    // people want, and it was not reachable at all.
    //
    // Release keeps a floor. Zero attack and zero decay are instants; zero
    // release is a click on every note-off, which is not a sound anybody is
    // asking for by dragging a knob to the bottom.
    { &ids::attack, " s", 0.0, 10.0, 0.005, 0.001, 3, ParamCurve::cubic },
    { &ids::decay, " s", 0.0, 10.0, 0.120, 0.001, 3, ParamCurve::cubic },
    { &ids::sustain, "", 0.0, 1.0, 0.700, 0.001, 3 },
    { &ids::release, " s", 0.002, 10.0, 0.150, 0.001, 3, ParamCurve::cubic },
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
    // Automatable, like the rest of a slot: switching an oscillator in and out
    // over a section is a patch change somebody wants to write down, and it is
    // the slot's own parameter rather than an effect's bypass - which is why
    // the engine spells it oscEnabled and not enabled.
    toggleSpec (&ids::enabled, /*automatable*/ true, /*defaultOn*/ true),

    // Four octaves either way, which is what the engine renders; the stepper
    // offered three.
    //
    // Automatable, and discrete: automationValueFor snaps it, so a curve lands
    // on a whole octave rather than between two of them.
    { &ids::octave, "", -4.0, 4.0, 0.0, 1.0, 0, ParamCurve::linear, ParamControl::stepper, true,
      /*automatable*/ true,
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
    { &ids::detuneCents, " c", -100.0, 100.0, 0.0, 1.0, 0, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true, /*automatable*/ true },

    { &ids::gain, "", 0.0, 1.0, 0.8, 0.01, 2 },

    choiceSpec (&ids::mode, oscModes, (int) std::size (oscModes), "classic", 0.0),

    // --- the slot's row of the FM matrix, from here --------------------------
    //
    // Three destinations and an output, which is one row of a 3x4 matrix: how
    // far this slot bends each slot's phase, and how much of it is heard.
    //
    // The DEFAULTS are the feature. Every amount is zero and every output is
    // one, which is three oscillators summed in parallel by their own gains -
    // exactly what this synth was before the matrix existed - so no committed
    // project, preset or pinned render moves. SnapshotReaders turns that into a
    // bank-wide `anyFm` flag and the voice keeps its old render path whenever
    // it is false, the way `lfoActive` already works.
    //
    // A slot CAN address itself: the diagonal is feedback, and it costs nothing
    // to allow because the modulator is read one sample late whichever cell it
    // came from - see SynthVoiceFm.cpp.
    //
    // Automatable, and the only oscillator parameters besides the wavetable
    // position that reach a note already sounding. An FM index that could only
    // change between notes is an FM index that never moves, which is the same
    // argument setWavetablePosition makes for itself.
    { &ids::fmTo1, "", 0.0, 1.0, 0.0, 0.01, 2 },
    { &ids::fmTo2, "", 0.0, 1.0, 0.0, 0.01, 2 },
    { &ids::fmTo3, "", 0.0, 1.0, 0.0, 0.01, 2 },

    // Multiplies `gain` rather than replacing it. A level says how loud this
    // oscillator IS - it is what it sends as a modulator too - and the output
    // column says how much of it goes to the speakers, which is the distinction
    // that lets a slot be a modulator and nothing else.
    { &ids::fmOut, "", 0.0, 1.0, 1.0, 0.01, 2 },

    // --- the CLASSIC generator's own, from here -----------------------------
    choiceSpec (&ids::wave, waveforms, (int) std::size (waveforms), "saw", 1.0),

    // --- the WAVETABLE generator's own, from here ---------------------------
    choiceSpec (&ids::wavetable, wavetables, (int) std::size (wavetables), "basic", 0.0),

    { &ids::wavePosition, "", 0.0, 1.0, 0.0, 0.01, 2 },
    { &ids::wavePositionMod, "", -1.0, 1.0, 0.0, 0.01, 2, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },

    choiceSpec (&ids::wavePositionSource, positionSources, (int) std::size (positionSources),
                "envelope", 0.0),

    { &ids::wavePositionRate, " Hz", 0.01, 20.0, 1.0, 0.01, 2, ParamCurve::logarithmic },

    { &ids::unisonVoices, "", 1.0, (double) kMaxUnisonVoices, 1.0, 1.0, 0, ParamCurve::linear,
      ParamControl::knob, false, /*automatable*/ false,
      /*integral*/ true },
    { &ids::unisonDetune, " c", 0.0, 50.0, 0.0, 0.5, 1 },

    // --- the SLOT's LFO, from here ------------------------------------------
    //
    // Appended, and that is the whole reason it is safe: the OSC group is a
    // PREFIX of this table (oscSpecs, kNumSlotParams) and each generator points
    // at a fixed offset from the front, so nothing above moves and not one
    // committed .dew is rewritten. A row inserted anywhere else would be.
    //
    // One LFO per SLOT rather than per generator: pitch, level and position in
    // the field are three things a slot has whichever generator it runs, so
    // this is a group of its own under the slot and not a row in either
    // generator's run. It is deliberately NOT in `generators` - see below.
    toggleSpec (&ids::lfoOn, /*automatable*/ false),

    // The SAME table the classic oscillator's shape comes from. A second
    // four-row list would be a second place "square" is spelled.
    //
    // Sine by default, because that is what somebody switching an LFO on to
    // hear it move is asking for; the classic oscillator defaults to saw
    // because that is what somebody is asking to HEAR.
    choiceSpec (&ids::lfoWave, waveforms, (int) std::size (waveforms), "sine", 0.0),

    toggleSpec (&ids::lfoSync, /*automatable*/ false),

    // The same range and curve wavePositionRate declares - it is the same
    // question asked about a different destination, so it takes the same
    // answer. Logarithmic because a rate is frequency-like: linearly, four
    // fifths of the travel would sit above 4Hz.
    { &ids::lfoRate, " Hz", 0.01, 20.0, 1.0, 0.01, 2, ParamCurve::logarithmic },

    choiceSpec (&ids::lfoDivision, noteDivisions, (int) std::size (noteDivisions), "quarter", 2.0),

    // Three destinations, three depths, all bipolar and all defaulting to
    // nothing - which is what makes "any combination" mean "set the ones you
    // want". Bipolar so two slots can be moved in antiphase against each other.
    //
    // Two octaves either way on the pitch, in SEMITONES rather than the cents
    // detuneCents uses: a detune is a handful of cents by definition and an LFO
    // to pitch is asked for in whole tones as often as in a vibrato's worth.
    { &ids::lfoToPitch, " st", -24.0, 24.0, 0.0, 0.01, 2, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },
    { &ids::lfoToVolume, "", -1.0, 1.0, 0.0, 0.01, 2, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },
    { &ids::lfoToPan, "", -1.0, 1.0, 0.0, 0.01, 2, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },
};

/** How the table above divides: the SLOT's own parameters first, then one run
    per generator, in the order the `mode` choice names them.

    Spans of one array rather than three arrays, so the schema still generates
    the node from a single declared table in a single order - which is the thing
    ProjectSchema's own comment asks for - while the registry can still say
    which parameters are whose. A generator that had its own array would be a
    second place the order lives.
*/
constexpr int kNumSlotParams = 9; ///< enabled, octave, detuneCents, gain, mode, the FM row

/** The FM row addresses one destination per slot BY NAME, so the table above
    stops describing the instrument the moment there is a fourth oscillator. */
static_assert (kMaxOscillators == 3,
               "the FM matrix declares one destination per oscillator slot by name");

constexpr int kNumClassicParams = 1;   ///< wave
constexpr int kNumWavetableParams = 7; ///< the table, its position and the unison stack
constexpr int kNumLfoParams = 8;       ///< the slot's LFO: its shape, its rate, its three depths

/** Where the LFO's run starts. Spelled once, because three places need it - the
    accessor below, the synthGroups row and nothing else may recompute it. */
constexpr int kLfoParamsOffset = kNumSlotParams + kNumClassicParams + kNumWavetableParams;

static_assert (kLfoParamsOffset + kNumLfoParams == (int) std::size (oscSpecs),
               "every oscillator parameter belongs to the slot, to one generator, or to "
               "the slot's LFO");

/** Every generator, and the run of oscSpecs each one alone reads.

    See GeneratorCatalog.h for why this is a table rather than a bool.
*/
const GeneratorDescriptor generators[] {
    { "classic", StringId::choice_oscMode_classic, &ids::CLASSIC, oscSpecs + kNumSlotParams,
      kNumClassicParams },
    { "wavetable", StringId::choice_oscMode_wavetable, &ids::WAVETABLE,
      oscSpecs + kNumSlotParams + kNumClassicParams, kNumWavetableParams },
};

static_assert (std::size (generators) == std::size (oscModes),
               "every generator the mode choice names has to have a table");

const ParamSpec mixerTrackSpecs[] {
    // The three-way disagreement the plan named: the fader offered 0..1.5, the
    // engine clamped at 2.0 and automation mapped onto 0..1, so automating a
    // fader reached two thirds of its travel and the top third of the engine's
    // range was unreachable from anywhere. The FADER wins: 2.0 is six decibels
    // no control ever offered.
    { &ids::gain, "", 0.0, 1.5, 0.8, 0.001, 3 },
    { &ids::pan, "", -1.0, 1.0, 0.0, 0.001, 3, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },

    // A track says `mute` where a channel says `muted`. Two spellings of one
    // idea, kept because both are already in every saved file.
    toggleSpec (&ids::mute, /*automatable*/ true),
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
    { &ids::fadeInMs, " ms", 0.0, 2000.0, 0.0, 1.0, 0, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ false, /*automatable*/ false },
    { &ids::fadeOutMs, " ms", 0.0, 2000.0, 0.0, 1.0, 0, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ false, /*automatable*/ false },
    // A double in the schema, so not integral here - see detuneCents.
    { &ids::transpose, "", -24.0, 24.0, 0.0, 1.0, 0, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true, /*automatable*/ false },

    toggleSpec (&ids::reverse, /*automatable*/ false),
    toggleSpec (&ids::loop, /*automatable*/ false),
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

    All six are automatable, through the channelSoundFont scope. What a curve
    means here is what it means everywhere else in dew except a wavetable
    position: a voice latches its settings at note-on, so a curve moves the NEXT
    note rather than bending the one already sounding. That is the honest
    reading of an offset into a font - a preset's envelope stages and its
    filter are decided when the region starts - and it is why these are offered
    rather than withheld: sweeping a filter offset across a section is exactly
    the thing somebody wants to draw.
*/
const ParamSpec soundFontSpecs[] {
    // A double in the schema, so not integral here - see detuneCents.
    { &ids::transpose, "", -24.0, 24.0, 0.0, 1.0, 0, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true, /*automatable*/ true },

    { &ids::tuneCents, " c", -100.0, 100.0, 0.0, 1.0, 0, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true, /*automatable*/ true },

    // Linear, and that is not an oversight: a cent IS a logarithmic unit, so
    // this offset is already in the domain ParamCurve::logarithmic exists to
    // reach. That curve could not be used here anyway - it maps min*(max/min)^v,
    // which needs a positive minimum, and half of this range is below zero.
    { &ids::filterOffset, " c", -2400.0, 2400.0, 0.0, 10.0, 0, ParamCurve::linear,
      ParamControl::knob, /*bipolar*/ true, /*automatable*/ true },

    // Multipliers rather than times, because that is what an offset in timecents
    // IS: the format stores envelope stages logarithmically, so adding to one
    // scales it. A quarter to four times, with 1 in the middle of the travel.
    { &ids::attackScale, "x", 0.25, 4.0, 1.0, 0.01, 2, ParamCurve::logarithmic, ParamControl::knob,
      /*bipolar*/ false, /*automatable*/ true },
    { &ids::releaseScale, "x", 0.25, 4.0, 1.0, 0.01, 2, ParamCurve::logarithmic, ParamControl::knob,
      /*bipolar*/ false, /*automatable*/ true },

    // How much of the format's velocity-to-attenuation curve is applied. 1 is
    // what the SF2 specification says; 0 plays every note at full level, which
    // is what a stepped pattern usually wants.
    { &ids::velocitySens, "", 0.0, 1.0, 1.0, 0.01, 2, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ false, /*automatable*/ true },
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
    { &ids::tempoBpm, " bpm", 20.0, 999.0, 128.0, 0.1, 1, ParamCurve::logarithmic,
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
    { &ids::CHANNEL, "", StringId::group_channel_name, channelSpecs, (int) std::size (channelSpecs),
      1,
      /*inPreset*/ false },
    { &ids::OSC, "oscillators", StringId::group_oscillator_name, oscSpecs, kNumSlotParams,
      kMaxOscillators },

    // One per slot, under it. The registry says which parameters are whose and
    // this says where they live; a third generator is a row in both.
    { &ids::CLASSIC, "classic", StringId::choice_oscMode_classic, oscSpecs + kNumSlotParams,
      kNumClassicParams, kMaxOscillators, /*inPreset*/ true, &ids::OSC },
    { &ids::WAVETABLE, "wavetable", StringId::choice_oscMode_wavetable,
      oscSpecs + kNumSlotParams + kNumClassicParams, kNumWavetableParams, kMaxOscillators,
      /*inPreset*/ true, &ids::OSC },

    // Under the slot like the two above, and for the same reason - but NOT a
    // generator. A generator is what a slot runs INSTEAD of another; the LFO is
    // what it does as well, whichever it is running. Putting it in `generators`
    // would fire the assertion tying that table to the `mode` choice, and would
    // make isForeignGeneratorParam hide these controls for half the modes.
    { &ids::LFO, "lfo", StringId::group_lfo_name, oscSpecs + kLfoParamsOffset, kNumLfoParams,
      kMaxOscillators, /*inPreset*/ true, &ids::OSC },

    { &ids::AMP, "amp", StringId::group_amp_name, ampSpecs, (int) std::size (ampSpecs) },
};

/** The audio channel's. Thin, and honestly so: everything that makes one
    recording differ from another is the recording, and the parameters are the
    handful of things done TO it. The file itself is deliberately not among
    them - a preset carrying a path points at somebody else's disk, and a trim
    measured in frames means nothing against another take. */
const ParamGroup audioGroups[] {
    { &ids::CHANNEL, "", StringId::group_channel_name, channelSpecs, (int) std::size (channelSpecs),
      1,
      /*inPreset*/ false },
    { &ids::SAMPLE, "sample", StringId::group_sample_name, sampleSpecs,
      (int) std::size (sampleSpecs) },
};

/** The soundfont channel's. Thin for the same reason the audio one is: what
    makes one soundfont differ from another is the file, and these are the
    handful of things done TO it.

    The file, the bank and the program are deliberately NOT here, so they do not
    travel in a preset - the same line the audio instrument draws. A preset
    carrying a path points at somebody else's disk, and a bank and program mean
    nothing against another font. */
const ParamGroup soundFontGroups[] {
    { &ids::CHANNEL, "", StringId::group_channel_name, channelSpecs, (int) std::size (channelSpecs),
      1,
      /*inPreset*/ false },
    { &ids::SOUNDFONT, "soundfont", StringId::group_soundfont_name, soundFontSpecs,
      (int) std::size (soundFontSpecs) },
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

const std::vector<ParamSpec>& oscSlotParamSpecs()
{
    static const std::vector<ParamSpec> table { oscSpecs, oscSpecs + kNumSlotParams };
    return table;
}

const std::vector<ParamSpec>& oscLfoParamSpecs()
{
    static const std::vector<ParamSpec> table { oscSpecs + kLfoParamsOffset,
                                                oscSpecs + kLfoParamsOffset + kNumLfoParams };
    return table;
}

const std::vector<GeneratorDescriptor>& generatorDescriptors()
{
    static const std::vector<GeneratorDescriptor> all { std::begin (generators),
                                                        std::end (generators) };
    return all;
}

const std::vector<InstrumentDescriptor>& instrumentDescriptors()
{
    static const std::vector<InstrumentDescriptor> all {
        { InstrumentType::synth, "synth", StringId::instrument_synth_name, synthGroups,
          (int) std::size (synthGroups) },
        { InstrumentType::audio, "audio", StringId::instrument_audio_name, audioGroups,
          (int) std::size (audioGroups) },
        { InstrumentType::soundfont, "soundfont", StringId::instrument_soundfont_name,
          soundFontGroups, (int) std::size (soundFontGroups) },
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
    return tr (instrumentDescriptor (type).displayName);
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
