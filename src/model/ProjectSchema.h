#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

#include "Ids.h"

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
    juce::String jsonKey;    ///< key this child occupies in the JSON object
    const NodeSpec* spec;    ///< the child node's own schema
    bool isArray;            ///< true: every child of this type becomes an array element

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
*/
inline constexpr int kFormatVersion = 7;

/** How many effects one channel or mixer track may carry. A document limit
    rather than an engine one: a chain longer than this cannot be saved, so it
    can never turn into an effect that silently stopped working.
*/
inline constexpr int kMaxEffectsPerChain = 4;

/** How many oscillators one channel carries.

    Fixed, not a maximum: every channel has exactly this many slots at all
    times, most of them switched off. A slot that only came into existence once
    it was enabled would leave the editor with nothing to show until you had
    already committed to using it.
*/
inline constexpr int kMaxOscillators = 3;

/** Value of the "format" key, so a wrong-but-valid JSON file is rejected with a
    useful message instead of loading as an empty project.
*/
inline constexpr const char* kFormatTag = "dew-project";

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
juce::ValueTree treeFromVar (const juce::var& value,
                             const NodeSpec& spec,
                             juce::StringArray& warnings,
                             const juce::String& path = "$");

} // namespace dew
