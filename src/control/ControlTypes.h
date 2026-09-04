#pragma once

#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

namespace dew::control
{

/** What an argument may hold.

    A closed, small vocabulary rather than JSON Schema's whole type system. It
    exists to be turned into a schema, not to reimplement one: five kinds cover
    every argument the operation table declares, and a kind added here fails to
    compile the switches that map it - in ControlValue.cpp and in the emitter -
    because -Wswitch-enum is an error under the ci preset. That is the trick
    DocsSchema.h's identifierOf already uses, and it is why this is an enum
    rather than a string.
*/
enum class ValueKind
{
    text,    ///< a JSON string
    integer, ///< a whole number
    number,  ///< any number
    flag,    ///< true or false
    object,  ///< a nested shape, described by `fields`
    array,   ///< a list of `fields`-shaped objects

    /** Whatever the thing being addressed takes.

        Exactly one argument in the whole table needs this - a parameter's
        value - and it needs it honestly: a cutoff is a number, an oscillator's
        mode is the id of a choice, and a toggle is a boolean, because that is
        what each of them is in the file. Declaring it `number` would make every
        choice unreachable, and splitting it into `value` and `text` would make
        every caller decide which one to send before it knows what it is
        writing.

        params_list says what a given parameter takes, which is where a caller
        finds out. Anything is accepted here and the spec does the checking.
    */
    any
};

const char* nameOfKind (ValueKind) noexcept;

/** One argument of one operation, and the whole of what is known about it.

    `fields` is recursive on purpose: almost every write in this table is
    batch-first, taking an array of entries, and a model can only fill an array
    of objects correctly if the object's own shape is in the schema it was
    handed. A flat argument list would have made every batch tool take an
    undescribed blob.

    A vector of an incomplete type is well defined here - std::vector is one of
    the three standard containers permitted to hold one - which is what lets the
    shape be declared inline beside the argument it belongs to rather than as a
    table of shapes somewhere else.
*/
struct ArgSpec
{
    /** A leaf: text, a number, a flag, or a parameter's own value.

        A constructor rather than aggregate initialisation, because the ci
        preset makes -Wmissing-field-initializers an error and a table of two
        hundred arguments would otherwise carry two hundred trailing `{}` for
        the one field that is usually empty. The nested form below is a second
        constructor rather than a defaulted parameter so that a shape passed by
        mistake to a leaf kind does not compile.
    */
    ArgSpec (juce::String argName, ValueKind argKind, bool isRequired, const char* argDoc)
        : name (std::move (argName))
        , kind (argKind)
        , required (isRequired)
        , doc (argDoc)
    {
    }

    /** An object or an array, and the shape of what it holds. */
    ArgSpec (juce::String argName, ValueKind argKind, bool isRequired, const char* argDoc,
             std::vector<ArgSpec> shape)
        : name (std::move (argName))
        , kind (argKind)
        , required (isRequired)
        , doc (argDoc)
        , fields (std::move (shape))
    {
    }

    /** A juce::String rather than a literal, so that an argument backed by a
        document property can be NAMED by that property: `ids::muted` reaches
        here through Identifier::toString and the wire name and the tree's name
        are then one spelling rather than two that agree today.

        That is also what the gate on parameters written as string literals is
        for. It is right to refuse `"muted"` beside `ids::muted`: the two would
        be free to drift, and a rename would leave the schema describing an
        argument the document no longer has.
    */
    juce::String name;
    ValueKind kind = ValueKind::text;
    bool required = false;

    /** One sentence, in the imperative, addressed to whoever is calling. It
        reaches both the tool's JSON schema and the website, so it is the only
        description of this argument that exists. */
    const char* doc = "";

    /** For `object` and `array`. Empty for every other kind.

        An array whose single field has an EMPTY NAME is an array of that field
        rather than an array of objects - which is what lets `ids` be a list of
        integers instead of a list of unconstrained anythings. A model given an
        untyped array sends whatever it likes into it, and the first thing it
        sends wrongly is a list of objects wrapping the id.
    */
    std::vector<ArgSpec> fields;

    /** True when `fields` describes the ELEMENT rather than an object's
        properties. See above; stated as a predicate so the validator and the
        schema emitter cannot read the convention two ways. */
    bool holdsBareElements() const
    {
        return kind == ValueKind::array && fields.size() == 1 && fields.front().name.isEmpty();
    }
};

/** Whether an operation only reads, or changes the document.

    The ONLY permission gate. A grant carries one of these and an operation
    carries one of these, and the check is that comparison - there is no second
    table of which tools are dangerous, free to disagree with what they do.
*/
enum class OpScope
{
    read,
    write
};

/** What an operation gives back.

    Deliberately not an exception and not a juce::Result: an operation that
    fails has something to SAY - which entry of a batch, and what was wrong with
    it - and that has to survive as far as the protocol layer, which turns it
    into a JSON-RPC error. A juce::Result carries a string and no structure.
*/
struct ControlResult
{
    bool ok = true;

    /** What the caller did wrong, in a sentence naming the offending value.
        Empty when ok. */
    juce::String error;

    /** The operation's answer. A void operation returns an object saying what
        it did rather than nothing: "applied" with a count is what lets a caller
        notice that its filter matched no rows, which is the failure a bare
        success hides. */
    juce::var value;

    static ControlResult failure (const juce::String& message)
    {
        return { false, message, {} };
    }

    static ControlResult success (juce::var v)
    {
        return { true, {}, std::move (v) };
    }
};

} // namespace dew::control
