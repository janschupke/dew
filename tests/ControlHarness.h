#pragma once

#include <catch2/catch_test_macros.hpp>

#include "FixtureProject.h"
#include "control/ControlHost.h"
#include "control/ControlOps.h"
#include "control/ControlValue.h"

namespace dew::testing
{

/** A ControlHost with nothing above it.

    This is the whole argument for ControlHost's existence, made concrete: a
    ValueTree, an UndoManager, and every operation in the table driveable
    against them. No window, no message loop, no audio device, no socket, no
    ScopedJuceInitialiser - so the operation suite runs in milliseconds and
    tests what an operation DOES rather than what the application does around
    it.

    Everything a headless caller cannot answer keeps ControlHost's own refusing
    default, which is also what a test wants to assert about: an operation that
    needs the transport must say so rather than crash.
*/
class FakeHost : public control::ControlHost
{
public:
    FakeHost()
        : state (fixtureProject())
    {
    }

    explicit FakeHost (juce::ValueTree tree)
        : state (std::move (tree))
    {
    }

    juce::ValueTree project() override
    {
        return state;
    }

    juce::UndoManager* undoManager() override
    {
        return &undo;
    }

    void flushEngine() override
    {
        ++flushes;
    }

    /** How many undo steps the history holds.

        The measurement every batch test makes, because "one call is one undo
        step" is the promise the consent dialog rests on and it is not
        observable any other way. juce::UndoManager has no depth accessor, so it
        is counted by undoing to the bottom on a COPY of the document - the real
        one would be left rewound.
    */
    int undoDepth()
    {
        auto count = 0;

        while (undo.canUndo())
        {
            undo.undo();
            ++count;
        }

        for (auto i = 0; i < count; ++i)
            undo.redo();

        return count;
    }

    juce::ValueTree state;
    juce::UndoManager undo;
    int flushes = 0;
};

/** Calls an operation by name, the way the protocol layer does.

    Through findOp rather than by calling a handler directly, so a test also
    proves the operation is IN the table - a handler that works and was never
    appended is a tool no client can reach.
*/
inline control::ControlResult call (control::ControlHost& host, const juce::String& name,
                                    const juce::var& args = {})
{
    const auto* op = control::findOp (name);

    REQUIRE (op != nullptr);

    // Validated first, exactly as the dispatcher does. A test that skipped this
    // could pass arguments no real client could send.
    const auto fault = control::validateArgs (op->args, args);

    INFO ("arguments rejected: " << fault);
    REQUIRE (fault.isEmpty());

    return op->handler (host, args);
}

/** A juce::var object, for building arguments in a test. Not `Args`, which is
    dew_i18n's message-format arguments and would be ambiguous in any test that
    uses both. */
struct Fields
{
    Fields()
        : object (new juce::DynamicObject())
    {
    }

    Fields& with (const char* key, const juce::var& value)
    {
        object->setProperty (juce::Identifier (key), value);
        return *this;
    }

    operator juce::var () const // NOLINT(google-explicit-constructor)
    {
        return juce::var (object.get());
    }

    juce::DynamicObject::Ptr object;
};

/** An array of vars, for the same. */
inline juce::var list (std::initializer_list<juce::var> items)
{
    juce::Array<juce::var> array;

    for (const auto& item : items)
        array.add (item);

    return juce::var (array);
}

} // namespace dew::testing
