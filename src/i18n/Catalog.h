#pragma once

namespace dew
{

/** One locale's text, as the generator lays it out.

    Parallel arrays indexed by StringId rather than a map, because the index IS
    the key: StringIds.h and this table are emitted from the same walk of the
    same file, in the same order, so a lookup is an array subscript and a key
    the app names that the catalogue does not hold is a compile error rather
    than a runtime miss.

    const char* rather than juce::String so the whole catalogue lives in the
    binary's read-only data and costs nothing until a locale is chosen. The
    bytes are UTF-8; see Strings.cpp for the one place that matters.
*/
struct Catalog
{
    const char* tag;         ///< BCP-47: "en", "de", "fr-CA"
    const char* const* text; ///< numStrings rows, "" where untranslated
};

/** The compiled-in catalogues, first being the reference locale. */
const Catalog* catalogs() noexcept;
int numCatalogs() noexcept;

/** Every key's dotted path - "transport.tempo.help" - indexed by StringId.

    The fallback for a missing row and the subject of the gate on keys nothing
    asks for. Not for lookup: nothing resolves a string by its path at runtime,
    because doing so would be a way to name a key the enum does not have.
*/
const char* const* catalogKeyPaths() noexcept;

/** The argument names each message asks for, space-separated, indexed by
    StringId. "" for a message that takes none.

    Recorded at generation time so a test can answer every message without
    knowing what any of them say - which is what makes "no brace reaches the
    screen" checkable rather than a thing somebody watches for.
*/
const char* const* catalogArgumentNames() noexcept;

} // namespace dew
