#include "i18n/Strings.h"

#include "i18n/Catalog.h"
#include "i18n/MessageFormat.h"

namespace dew
{

namespace
{

/** The active locale's text, built once.

    Function-local rather than a namespace-scope object so that a tr() reached
    from a static initialiser somewhere else still finds a constructed table.
*/
struct ActiveCatalogue
{
    juce::String tag;
    std::vector<juce::String> text;
};

ActiveCatalogue& active()
{
    static ActiveCatalogue catalogue;
    return catalogue;
}

/** The reference locale is the first the generator wrote, and it is what every
    other locale falls back to row by row. */
int referenceIndex() noexcept
{
    return 0;
}

int indexOfLocale (juce::StringRef tag) noexcept
{
    const auto* table = catalogs();

    for (auto i = 0; i < numCatalogs(); ++i)
        if (tag == juce::StringRef (table[i].tag))
            return i;

    return -1;
}

/** An exact tag, then the language alone, then the reference.

    fr-CA reaching fr is the whole point: a build that ships one French
    catalogue should serve every French-speaking region from it rather than
    falling all the way back to English.
*/
int negotiate (juce::StringRef wanted) noexcept
{
    if (const auto exact = indexOfLocale (wanted); exact >= 0)
        return exact;

    const auto language = juce::String (wanted).upToFirstOccurrenceOf ("-", false, false);

    if (const auto byLanguage = indexOfLocale (language); byLanguage >= 0)
        return byLanguage;

    return referenceIndex();
}

void buildFor (int localeIndex)
{
    const auto* table = catalogs();
    const auto& chosen = table[localeIndex];
    const auto& reference = table[referenceIndex()];
    const auto* paths = catalogKeyPaths();

    auto& catalogue = active();

    catalogue.tag = juce::String (chosen.tag);
    catalogue.text.clear();
    catalogue.text.reserve ((size_t) numStrings);

    for (auto i = 0; i < numStrings; ++i)
    {
        // CharPointer_UTF8, and this is the one place in dew where that
        // distinction is load-bearing rather than a style. juce::String's
        // const char* constructor decodes ASCII and mangles every non-ASCII
        // byte the catalogue holds; the gate at SourceGateTests "no source
        // starts a concatenation with a non-ASCII literal" exists because that
        // mistake is invisible in review.
        const auto* row = chosen.text[i];

        // A locale that does not translate a row gets the reference's, so a
        // partial translation is partial rather than blank.
        if (row == nullptr || *row == '\0')
            row = reference.text[i];

        // And a row the reference itself does not hold returns its own key -
        // never empty. See the note in Strings.h about the two coverage gates
        // that a blank would quietly disarm.
        if (row == nullptr || *row == '\0')
            catalogue.text.emplace_back (juce::CharPointer_UTF8 (paths[i]));
        else
            catalogue.text.emplace_back (juce::CharPointer_UTF8 (row));
    }
}

ActiveCatalogue& ensureBuilt()
{
    auto& catalogue = active();

    if (catalogue.text.empty())
        buildFor (referenceIndex());

    return catalogue;
}

} // namespace

const juce::String& tr (StringId id)
{
    auto& catalogue = ensureBuilt();
    const auto index = (size_t) id;

    jassert (index < catalogue.text.size());

    return catalogue.text[index];
}

juce::String tr (StringId id, const Args& arguments)
{
    return formatMessage (tr (id), arguments, activeLocale());
}

void setLocale (juce::StringRef tag)
{
    buildFor (negotiate (tag));
}

juce::String activeLocale()
{
    return ensureBuilt().tag;
}

juce::StringArray availableLocales()
{
    juce::StringArray tags;
    const auto* table = catalogs();

    for (auto i = 0; i < numCatalogs(); ++i)
        tags.add (juce::String (table[i].tag));

    return tags;
}

juce::String endonymOf (juce::StringRef tag)
{
    // clang-format off
    struct Endonym { const char* tag; const char* name; };

    // \u escapes rather than raw bytes or \x: a \x escape swallows every hex
    // digit that follows it, so "Fran\xc3\xa7ais" reads \xa7a as one character
    // and does not compile. These are the only non-ASCII literals in dew's own
    // source, and they are constructed through CharPointer_UTF8 below for the
    // reason the concatenation gate exists.
    static const Endonym endonyms[] {
        { "en", "English" },
        { "de", "Deutsch" },
        { "fr", "Fran\u00e7ais" },
        { "es", "Espa\u00f1ol" },
        { "cs", "\u010ce\u0161tina" },
        { "fi", "Suomi" },
        { "ja", "\u65e5\u672c\u8a9e" },
    };

    // clang-format on

    const auto language = juce::String (tag).upToFirstOccurrenceOf ("-", false, false);

    for (const auto& row : endonyms)
        if (language == juce::StringRef (row.tag))
            return juce::String (juce::CharPointer_UTF8 (row.name));

    return juce::String (tag);
}

juce::String keyOf (StringId id)
{
    return juce::String (catalogKeyPaths()[(size_t) id]);
}

juce::StringArray argumentNamesOf (StringId id)
{
    juce::StringArray names;
    names.addTokens (juce::String (catalogArgumentNames()[(size_t) id]), " ", {});
    names.removeEmptyStrings();

    return names;
}

} // namespace dew
