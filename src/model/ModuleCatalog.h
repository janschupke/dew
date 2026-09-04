#pragma once

#include <optional>
#include <vector>

#include "i18n/Strings.h"
#include "model/EffectType.h"
#include "model/InstrumentType.h"
#include "model/ParamRole.h"
#include "model/ParamSpec.h"

namespace dew
{

/** What one effect is: what it is called, what it is called in a file, and
    every parameter it has.

    The registry is an explicit table joined to the enum, not a set of
    self-registering objects. These are static libraries built without
    --whole-archive, so a `static Registrar r;` in an effect's own translation
    unit would be dropped by the linker and the type would vanish with no error
    anywhere. A table the compiler can count is worth more than the convenience.
*/
struct EffectDescriptor
{
    EffectType type;
    const char* id; ///< "filter" - what a .dew stores
    StringId displayName;
    const ParamSpec* params;
    int numParams;
};

/** Every effect, indexed by EffectType. */
const std::vector<EffectDescriptor>& effectDescriptors();

const EffectDescriptor& effectDescriptor (EffectType) noexcept;

/** The type a stored id names, or nothing.

    Returns an optional rather than falling back to `filter`, which is what the
    old string reader did: an unrecognised type silently became a low-pass
    filter, with no warning, and the project sounded wrong in a way nothing
    reported.
*/
std::optional<EffectType> effectTypeFor (juce::StringRef id);

juce::String effectTypeToString (EffectType);
juce::String effectTypeDisplayName (EffectType);

/** How many floats a slot's parameter block holds: one common parameter, then
    the widest type's own.

    A choice like the filter's mode is a float in the block, the way a discrete
    parameter is a float in every plugin API - which keeps the module interface
    one shape rather than one shape plus an exception.
*/
inline constexpr int kNumCommonEffectParams = 1;

/** Where `mix` sits, for every type. The host reads it without asking the
    module, which is what keeps dry/wet identical across all six. */
inline constexpr int kMixParamIndex = 0;
inline constexpr int kMaxEffectParams = 8;

/** Where a parameter sits in its slot's block, or -1 if it is not one of them.

    Index 0 is always `mix`; a type's own parameters follow in descriptor order.
*/
int effectParamIndex (EffectType, const juce::Identifier&) noexcept;

/** The parameters every effect has, whatever its type.

    Just `mix`, and it is deliberately not in the per-type tables: the host
    applies dry/wet identically for every type, which is what keeps "a fully dry
    slot is bit-exact passthrough" true in exactly one place.
*/
const std::vector<ParamSpec>& commonEffectParams();

/** Every parameter a slot of this type has, in the order a person should meet
    them: the type's own first, then the common ones.

    Deliberately not the order effectSpec() emits, which puts `mix` first
    because that is where the file has always had it. A file's key order and a
    panel's control order are different questions that happen to concern the
    same list.
*/
std::vector<ParamSpec> effectParamsFor (EffectType);

/** One node's worth of an instrument's parameters.

    An effect is a single node with a flat list; an instrument is a channel,
    three oscillator slots and an envelope. Naming that rather than flattening
    it keeps the AudioProcessor-shaped question - "enumerate every parameter of
    this type" - answerable in one walk, while leaving the tree the schema
    builds and the editor points at.
*/
struct ParamGroup
{
    const juce::Identifier* node; ///< ids::CHANNEL, ids::OSC, ids::AMP, ids::SAMPLE
    const char* jsonKey;          ///< "oscillators", "amp"; empty for the channel itself
    StringId displayName;
    const ParamSpec* params;
    int numParams;

    /** 1, or kMaxOscillators for a fixed array of slots. */
    int count = 1;

    /** Whether a preset carries this group. False for the channel's own
        parameters: they are the instrument's, but they are not its SOUND. */
    bool inPreset = true;
};

/** What one instrument is, in the same terms an effect is.

    The accessors below are named for their effect twins deliberately. That
    symmetry is the point: given either descriptor, one walk can enumerate every
    parameter of a type, read the whole state and write it back - which is what
    a preset is, and what an AudioProcessor wrapper would need.
*/
struct InstrumentDescriptor
{
    InstrumentType type;
    const char* id; ///< "synth" - what a .dew stores in a channel's `source`
    StringId displayName;
    const ParamGroup* groups;
    int numGroups;
};

/** Every instrument, indexed by InstrumentType. */
const std::vector<InstrumentDescriptor>& instrumentDescriptors();

const InstrumentDescriptor& instrumentDescriptor (InstrumentType) noexcept;

/** The type a stored `source` names, or nothing.

    An optional rather than a fallback, for the reason effectTypeFor is one:
    buildSnapshot used to read this with a ternary, so any unrecognised source
    became a synth with nothing said and a project written by a newer dew
    played back wrong and silently.
*/
std::optional<InstrumentType> instrumentTypeFor (juce::StringRef id);

juce::String instrumentTypeToString (InstrumentType);
juce::String instrumentTypeDisplayName (InstrumentType);

/** An effect's one group, so a caller that walks parameters walks both kinds of
    module the same way. An effect is the degenerate case of an instrument: one
    node, every parameter on it. */
ParamGroup effectGroup (EffectType) noexcept;

// --- the instrument's own parameters -----------------------------------------

/** A channel's own continuous parameters: volume, pan, base pitch.

    Declared here for the same reason an effect's are. Before this the amp
    envelope, the oscillators and the channel each had their range written out
    twice - once as an engine clamp and once as a knob - and all three had
    drifted:

        attack   engine 0.0005..10s   knob 0.0005..2s
        decay    engine 0.0005..10s   knob 0.0005..4s
        release  engine 0.002..10s    knob 0.002..4s
        octave   engine -4..+4        knob -3..+3

    In every case the knob was the narrower, so the top of the range the engine
    renders was simply unreachable - a four-second release on an instrument that
    can hold ten.
*/
const std::vector<ParamSpec>& channelParamSpecs();

/** The amplitude envelope. */
const std::vector<ParamSpec>& ampParamSpecs();

/** One oscillator slot's continuous parameters. */
const std::vector<ParamSpec>& oscParamSpecs();

/** The arrangement's own: the tempo, and nothing else. */
const std::vector<ParamSpec>& projectParamSpecs();

/** A sample's fades, its transpose, and its two switches. None automatable -
    see the table - but declared so a control has one place to take its range,
    its decimals and its default from. */
const std::vector<ParamSpec>& sampleParamSpecs();

/** A soundfont channel's offsets onto the font: pitch, tuning, filter, the two
    envelope multipliers and velocity sensitivity. Offsets rather than values,
    because an SF2 preset colours an instrument the same way - see the table. */
const std::vector<ParamSpec>& soundFontParamSpecs();

/** A mixer track's fader and pan.

    Separate from the channel's, because `gain` and `pan` mean different things
    on the two and had different ranges - and because the fader was the worst
    of the three-way disagreements: 1.5 on the control, 2.0 in the engine and
    1.0 through automation, so automating a fader reached two thirds of its
    travel and nothing could reach the top of the engine's.
*/
const std::vector<ParamSpec>& mixerTrackParamSpecs();

/** One of the above by property, or nothing.

    A lookup rather than an index, because these are read one at a time by the
    thing that owns the property - the envelope reader wants `attack`, not the
    third row of a table.
*/
const ParamSpec* instrumentParamSpec (const juce::Identifier&) noexcept;

/** The spec for a property, or a hard failure. For a call site that has no
    sensible behaviour without one - a knob cannot be built from nothing. */
const ParamSpec& requireInstrumentParamSpec (const juce::Identifier&);

/** The same, for a mixer track. A separate lookup rather than one table,
    because `gain` and `pan` appear in both and mean different things. */
const ParamSpec* mixerTrackParamSpec (const juce::Identifier&) noexcept;
const ParamSpec& requireMixerTrackParamSpec (const juce::Identifier&);

} // namespace dew
