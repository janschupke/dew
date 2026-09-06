#pragma once

#include <juce_core/juce_core.h>

#include "i18n/StringIds.h"
#include "i18n/Strings.h"

#include <utility>

namespace dew
{

/** Text that has already been through the catalogue.

    A parameter type, and the whole of what it does is REFUSE a literal. Every
    other guard on English written into a source file is a scanner: the gate in
    SourceGateStringsTests reads a call, asks what its argument starts with, and
    is defeated by any indirection at all. A helper that takes a juce::String is
    exactly that indirection - paint::emptyState drew ten sentences of raw
    English for a year, because the drawText the gate CAN see was inside the
    helper and its argument was a variable by then.

    Two implicit constructors and one deleted one. A StringId resolves through
    tr, an already-resolved juce::String passes through - a message with
    arguments, or a name a person typed - and a const char* is a compile error
    rather than a report from a test. A string LITERAL cannot reach the
    juce::String constructor either, because that would be two user-defined
    conversions in one implicit sequence, which the language does not allow; the
    deleted overload is here so the error names the rule rather than the
    conversion.

    It does NOT claim the text is translated - a juce::String assembled with +
    converts as happily as tr's does, and no type can tell them apart. That is
    the prose gate's job. This one closes the hole a scanner cannot see: the
    call site where the sentence is written.
*/
class Translated
{
public:
    Translated (StringId id)
        : text (tr (id))
    {
    }
    Translated (juce::String resolved)
        : text (std::move (resolved))
    {
    }
    Translated (const char*) = delete;

    operator const juce::String& () const noexcept
    {
        return text;
    }

    const juce::String& get() const noexcept
    {
        return text;
    }

private:
    juce::String text;
};

} // namespace dew
