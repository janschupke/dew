#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

#include "model/Ids.h"

namespace dew
{

/** The .dew schema, declared once as a table of node specs.

    That table drives conversion in both directions plus validation, so there is
    no hand-written serializer to fall out of step with a hand-written parser.

    Compatibility rules, asserted in the tests:
      - a property missing from the file takes its declared default, so files
        written by an older version still load;
      - a property whose value is the wrong type takes its default and produces
        a warning rather than a hard failure;
      - a key not in the schema is dropped, and reported as a warning.
*/
struct PropSpec
{
    juce::Identifier id;
    juce::var defaultValue;
};

struct NodeSpec;

struct ChildSpec
{
    juce::String jsonKey; ///< key this child occupies in the JSON object
    const NodeSpec* spec; ///< the child node's own schema
    bool isArray;         ///< true: every child of this type becomes an array element

    /** Arrays with a fixed number of slots. 0 means variable length.

        A fixed array is materialised at its full length everywhere the schema
        builds a node - defaults, canonicalisation and loading - so the editor
        always has every slot to point at, whether the document came from a
        file, from the factory or from a tree a test assembled by hand. A file
        carrying more than this many is truncated and the excess reported.
    */
    int fixedCount = 0;

    /** How slot `index` is created when the schema has to materialise it.

        Lets a fixed array have per-slot defaults - oscillator 1 on, the rest
        off - without a second table beside this one. Null means "the node's
        declared defaults", which is right for anything uniform.
    */
    juce::ValueTree (*makeSlot) (const NodeSpec&, int index) = nullptr;

    /** Leave this child OUT of the file when every property on it is still its
        declared default.

        For the node an oscillator slot carries for the generator it is NOT
        running. Both are always present IN MEMORY, which is what keeps the
        canonical tree one shape and lets the editor point at a generator before
        you have committed to it; a file does not need the editor's convenience.
        Without this, every classic slot in every project stored seven wavetable
        properties nothing would ever read - twelve of them per example.

        Lossless, and that is the whole reason it is "when default" rather than
        "when inert": a slot somebody dialled a wavetable into and then switched
        back still writes it, so switching a generator is not a way to lose the
        other one's settings on the next save. A child that IS at its defaults
        reads back as exactly what it was, because treeFromVar materialises a
        missing one from those same defaults.
    */
    bool omitWhenDefault = false;
};

struct NodeSpec
{
    juce::Identifier type;
    std::vector<PropSpec> props;
    std::vector<ChildSpec> children;
};

/** The schema version this build writes. Bumped when the format changes in a
    way that older builds cannot read.

    v2 added channel solo, and mute and solo on playlist tracks. Version 1 files
    still load: every added property has a declared default, so an older file is
    simply one that predates them.

    v6 replaced the single "osc" object with an "oscillators" array of fixed
    slots. That one is a shape change rather than an added property, so it needs
    a real migration; see ProjectSerializer.

    v7 added audio channels: `source` on a channel, a SAMPLE child beside the
    instrument, and `channelId` on a clip. All three are additive with declared
    defaults, so treeFromVar materialises them for a v6 file and no migration is
    needed. The version is bumped anyway because an OLDER build reading a v7
    file would drop the audio silently rather than refuse it, which is exactly
    what the version gate is for.

    v8 added the wavetable oscillator: `mode` on an oscillator slot plus the
    seven properties a wavetable slot reads. Additive with declared defaults
    like v7, so a v6 or v7 file needs no migration - every slot in it simply
    predates the mode and loads as "classic", which is what it was. Bumped for
    the same reason v7 was: an older build would drop the wavetable settings
    without a word rather than say it cannot read the file.

    v9 added the meter: `beatsPerBar` and `beatUnit` on the project. Additive
    with declared defaults of 4 and 4, so every earlier file loads as 4/4 -
    which is what it was, since four beats to the bar used to be a compile-time
    constant. Bumped because an older build would read a 7/8 project and play
    it in 4/4 without saying so, which puts every bar line and every clip
    boundary somewhere else.

    v10 added the score: a SCORE node holding the arrangement language's source
    text one LINE at a time, plus `genId` on the four node types a compile
    writes and `genHash` on a pattern. Additive with declared defaults - an
    empty score and empty provenance strings - so every earlier file loads as
    what it was: a project nobody compiled. Bumped for the reason v7 and v8
    were: an older build would open a project, drop its score text on the next
    save, and say nothing about it.

    v11 made an automation point's `step` a double and gave a point a `shape`.
    The shape is additive with a declared default of "curve", which with a bend
    of zero is exactly the straight line every earlier file already drew. The
    step is NOT additive: it is the same key changing type, so a v11 file
    written with a point at step 6.5 loads into an older build as step 6, which
    moves it. That is the one thing the version gate is for, and it is why this
    bump is not optional.

    v12 gave a playlist track and a mixer track a `colour`, which only a channel
    had. Additive, and the declared default is the EMPTY string rather than a
    hex value: empty means "inherit", which is what both of them did before -
    a lane took its colour from its position in the list and a strip took it
    from the channels routed into it. So an earlier file loads looking exactly
    as it did, and a v12 file opened in an older build loses a colour somebody
    chose rather than being misread. Bumped for that: losing it silently on the
    next save is what the version gate is for.

    v13 gave every channel a SOUNDFONT node, so a channel can play a soundfont
    file the way it can play a recording. Additive, and inert on the channels
    that do not use it - the same shape the SAMPLE node has had since v7, and
    for the same reason: the canonical tree is one shape whatever a channel
    turns out to be. An earlier file loads as what it was, and a v13 file opened
    in an older build loses the font a channel was pointed at rather than
    misreading it. Bumped for that.

    v14 added four effect types - distortion, phaser, compressor and limiter -
    and with them nine parameters on the one EFFECT node every type shares.
    Additive with declared defaults, so a v13 file loads as exactly what it was.
    The bump is for the other direction: an older build has no descriptor for
    "compressor", so effectTypeFor returns nothing and the slot would be dropped
    on the next save with nothing said. The version gate turns that into a
    refusal, which is the whole reason it exists.

    v15 moved a slot's generator parameters onto that generator's own node:
    CLASSIC holds `wave`, WAVETABLE holds the table, the position and the unison
    stack, and the slot keeps what is the SLOT's - whether it is on, its octave,
    its detune, its gain and which generator it runs.

    A shape change, so it needs a real migration and gets one; see
    migrateGeneratorParamsToNodes, which moves BOTH halves rather than only the
    one the slot is running, so loading and saving a v14 file is not a way to
    lose settings somebody dialled in before switching back.

    Both nodes are always present and one of them is inert, which is the shape
    every channel already has - it carries a SAMPLE and a SOUNDFONT whichever
    kind it is. What it buys is that a slot the factory writes no longer stores
    the seven wavetable properties a classic oscillator never reads.

    v16 gave a playlist track a `volume`, which scales the triggers a lane
    contributes rather than a bus - see PlaylistTrackHeader and Sequencer. It
    is additive with a declared default of 1.0, which is what every lane did
    when it could not carry one, so an earlier file loads as what it was. The
    bump is for the other direction, as it has been since v7: an older build
    would drop a lane's balance on the next save and say nothing.

    v17 made the amplitude envelope, a soundfont channel's six offsets and an
    oscillator slot's octave, detune and on/off automatable, which needed two
    new automation scopes - `channelAmp` and `channelSoundFont`. No property was
    added, moved or retyped, so there is nothing to migrate in either direction
    and a v16 file loads as exactly what it was.

    The bump is the same argument v14 makes about an effect type an older build
    has no descriptor for. automationScopeFromString falls back to `channel` for
    a spelling it does not know, and no channel parameter is called `attack` or
    `filterOffset`, so a v16 build opening a v17 project would resolve every
    envelope and soundfont curve to nothing and drop it on the next save without
    a word. The version gate turns that into a refusal.

    v18 gave every oscillator slot an LFO, on an `lfo` node beside the slot's
    two generator nodes: a shape, a rate that is either free or locked to the
    tempo, and three depths - to pitch, to level and to pan - so any combination
    of the three moves and the ones left at zero do not.

    Additive with declared defaults, so there is nothing to migrate: a v17 file
    simply has no `lfo` key and takes an LFO that is switched off, which is what
    it had. It also writes exactly the bytes it wrote before, because the node
    is declared omitWhenDefault and a slot nobody has dialled an LFO into has
    nothing to say.

    The bump is for the other direction, as it has been since v7: a v17 build
    would report the `lfo` node as not part of the schema, drop it on the next
    save, and hand back a project whose movement had gone.

    v19 gave every oscillator slot a row of an FM matrix, on the slot itself
    beside its gain: how far it bends each slot's phase, and how much of it is
    heard. Three amounts and an output, so a slot can be a modulator, a carrier,
    both, or - on the diagonal - its own.

    Additive, and the defaults are the old behaviour exactly: every amount is
    zero and every output is one, which is three oscillators summed in parallel.
    A v18 file has none of the four keys, takes those defaults, and renders the
    same samples it always did. There is nothing to migrate.

    Unlike the LFO's, these are NOT omitWhenDefault - they are properties of the
    OSC node rather than a node of their own, and the schema omits nodes and not
    keys - so every committed example and preset gains four keys per slot and
    was rewritten in the same commit.

    The bump is for the other direction, as ever: a v18 build would report all
    four as not part of the schema and drop them on the next save, handing back
    a project whose routing had silently become parallel again.

    v20 moved a clip from BARS to STEPS. `startBar` and `lengthBars` become
    `startStep` and `lengthSteps`, multiplied by the file's own stepsPerBar -
    which is why the migration has to read the metre before it converts
    anything, and why it is a real migration rather than a rename.

    A shape change with a purpose: a clip could only ever start on a bar line,
    so a fill that begins on the last beat of a bar was not expressible in this
    format at all. It also makes a clip metre-INVARIANT - setMeter's rescale of
    every clip in the arrangement is gone with this, because a step is a step
    whatever a bar is worth, which is exactly what that rescale existed to
    emulate.

    The bump is for the other direction as usual: a v19 build reads no
    `startStep`, so every clip in the arrangement would take the default and
    pile up at bar one.
*/
inline constexpr int kFormatVersion = 20;

/** How many effects one channel or mixer track may carry. A document limit
    rather than an engine one: a chain longer than this cannot be saved, so it
    can never turn into an effect that silently stopped working.
*/
inline constexpr int kMaxEffectsPerChain = 9;

/** How many inserts the mixer may hold, master excluded.

    A document limit for the same reason the chain length is one - now that
    inserts can be added, a project holding more of them than the engine renders
    would be a project with faders that move nothing. It lived in
    engine/EngineSnapshot.h while the count was fixed at four and nothing could
    change it; the engine still sizes its buffers from it and still clamps, and
    EngineSnapshot.h includes this header, so it reads the same number it always
    did. Putting the rule where the edit is means a hand-written .dew file
    cannot walk past it either.
*/
inline constexpr int kMaxMixerTracks = 32;

/** How many inserts a NEW project starts with.

    A number rather than a literal in the factory's loop, which is where it
    lived while it was four and where nothing could see it. Twenty because a
    mixer you have to add to before you can route anything is a mixer that
    reads as broken, and because the strip is 72px: twenty of them is about the
    width of a window, which is the point at which pinning the master stopped
    being cosmetic. Still well under kMaxMixerTracks. */
inline constexpr int kDefaultMixerTracks = 20;

/** How many oscillators one channel carries.

    Fixed, not a maximum: every channel has exactly this many slots at all
    times, most of them switched off. A slot that only came into existence once
    it was enabled would leave the editor with nothing to show until you had
    already committed to using it.
*/
inline constexpr int kMaxOscillators = 3;

/** How many detuned copies of itself one wavetable slot may stack.

    Capped because a voice preallocates every copy's phase, and 64 channels of
    16 voices exist at all times. Seven is odd on purpose: the spread is
    symmetric about a centre voice that sits at the note's own pitch.
*/
inline constexpr int kMaxUnisonVoices = 7;

/** Value of the "format" key, so a wrong-but-valid JSON file is rejected with a
    useful message instead of loading as an empty project.
*/
inline constexpr const char* kFormatTag = "dew-project";

/** Reads `value` as the type of `fallback`. Returns false if the value is
    present but unusable, so the caller can warn and fall back.

    Exposed because it is THE answer to "read this var as the type of that
    default", and a preset needs the same one. The type of a declared default
    is what decides whether a stored number is rounded, so a second copy of
    this rule would be a second answer to that.
*/
bool coerceToTypeOf (const juce::var& fallback, const juce::var& value, juce::var& out);

/** The root of the schema table. */
const NodeSpec& projectSpec();

/** Looks up a child spec by its JSON key, so callers name what they want
    ("channels") instead of indexing into the table by position.
    Asserts if the key is not part of the spec.
*/
const NodeSpec& childSpecFor (const NodeSpec& parent, juce::StringRef jsonKey);

/** Builds a tree containing every node and property in the spec at its default
    value. Used as the starting point for loading and for a new project.
*/
juce::ValueTree defaultTreeFor (const NodeSpec& spec);

/** Reorders a tree's children into schema order, recursively, returning a copy.

    The schema defines a canonical shape, and loading a file always produces it
    (treeFromVar walks the spec in order). A tree assembled by hand does not,
    because children get appended in whatever order the code ran. Without this,
    save-then-load would silently reshape a project - the data would survive but
    the tree would not compare equal, and anything depending on child order
    would see a different document than the one that was saved.
*/
juce::ValueTree canonicalTree (const juce::ValueTree& tree, const NodeSpec& spec);

/** ValueTree -> var, following the spec. Properties not in the spec are not
    written, so the output is always schema-shaped.
*/
juce::var varFromTree (const juce::ValueTree& tree, const NodeSpec& spec);

/** var -> ValueTree, following the spec. Never fails: anything unreadable falls
    back to the declared default and is appended to `warnings`.
*/
juce::ValueTree treeFromVar (const juce::var& value, const NodeSpec& spec,
                             juce::StringArray& warnings, const juce::String& path = "$");

} // namespace dew
