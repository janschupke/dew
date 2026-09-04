#include <catch2/catch_test_macros.hpp>

#include "SourceScan.h"

using namespace dew::testing;

namespace
{

/** True when `line` hands a sink that a PERSON reads a string literal.

    Named sinks rather than "any literal", for the reason the automatable-
    parameter gate is scoped rather than general: dew's sources are full of
    literals that are not English - node types, file-format ids ("filter",
    "lowpass"), icon names, JSON keys, colour token names. A gate that flagged
    those would cry wolf, and a gate that cries wolf is one people turn off.

    The EMPTY literal is allowed, and that is not an oversight. A DewIconButton
    is constructed with {} and told what it is a line later, which is exactly
    the shape "a control's tooltip and its accessible name are the same
    sentence" exists to hold.

    Sinks are added here as each one is emptied across the tree, so this gate
    has never been red on the tree it guards - a gate landed before its subject
    is a gate somebody disables to get on with the extraction.
*/
bool showsALiteral (const juce::String& line)
{
    for (const auto* sink :
         { "setTooltip (", "setButtonText (", "setText (", "setTitle (", "setSuffix (" })
    {
        if (! line.contains (sink))
            continue;

        const auto rest = line.fromFirstOccurrenceOf (sink, false, false).trimStart();

        if (rest.startsWith ("\"") && ! rest.startsWith ("\"\""))
            return true;
    }

    return false;
}

} // namespace

TEST_CASE ("no source shows a person a string literal", "[build][gate][i18n]")
{
    // The whole point of the catalogue. A sentence written into a source file
    // is a sentence no translator will ever see, and nothing else in the build
    // would report it.
    //
    // JUCE's own answer - TRANS() over LocalisedStrings - was rejected because
    // it keys on the ENGLISH TEXT, so correcting a typo silently orphans every
    // translation of it. dew's keys are structural, so a typo is a one-word
    // edit to en.json and nothing downstream notices.
    const auto found = offenders (showsALiteral);

    INFO ("English written into the source:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());

    // Control case: a scanner that cannot see the defect it was written for is
    // not a gate. Both lines below are the transport bar's tempo field, before
    // and after.
    CHECK (showsALiteral ("    tempoField.setTooltip (\"Tempo - drag up and down\");"));
    CHECK_FALSE (
        showsALiteral ("    tempoField.setTooltip (tr (StringId::transport_tempo_help));"));
    CHECK_FALSE (showsALiteral ("    icon.setTooltip (\"\");"));
    CHECK_FALSE (showsALiteral ("    button.setComponentID (\"mute\");"));
}
