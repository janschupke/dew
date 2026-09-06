#pragma once

#include <juce_core/juce_core.h>

#include <utility>

namespace dew
{

/** The name of an undo step, and the whole of what it does is say so.

    An undo name is one of the few English literals in dew that is NOT a
    sentence a person reads: getUndoDescription is called by nothing outside
    the tests, and the Edit menu shows the command's name rather than the
    transaction's - so translating the fifty-odd of them would buy a translator
    fifty keys nobody can reach. That exemption is right, and it used to be
    unstateable.

    The reason is that the literal arrives through four different call shapes -
    setProperty, setPropertyOnEvery, and the attach and write helpers of five
    panels - and a gate cannot tell "Rename channel" as a fifth argument from a
    sentence somebody wrote at a sink it does not know. Excusing those shapes
    by NAME would mean excusing setProperty and write outright, which are
    generic enough to hide a real offence. JUCE's own beginNewTransaction is
    the fifth shape and needs none of this: the name is its only argument and
    naming an undo step is all it does.

    Named instead. `TransactionName { "Rename channel" }` is a shape a gate can
    strip exactly, in the way it already strips a diagnostic's code and an
    argument's name, and it is the first time the tree can be asked what its
    undo steps are called.

    EXPLICIT, deliberately. An implicit constructor would let the literal go on
    being written bare, which leaves nothing to strip and nothing to grep.

    A juce::String overload as well, for the one name in the tree that is not a
    literal: loading a preset names the step after the preset.
*/
struct TransactionName
{
    explicit TransactionName (const char* text)
        : name (text)
    {
    }

    explicit TransactionName (juce::String text)
        : name (std::move (text))
    {
    }

    juce::String name;
};

} // namespace dew
