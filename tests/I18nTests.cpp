#include <catch2/catch_test_macros.hpp>

#include "i18n/MessageFormat.h"
#include "i18n/PluralRules.h"
#include "i18n/Strings.h"

using namespace dew;

TEST_CASE ("a message substitutes what it is given", "[i18n]")
{
    CHECK (formatMessage ("Reset {param}", Args {}.with ("param", "Cutoff"), "en")
           == "Reset Cutoff");

    CHECK (formatMessage ("{owner} > {param}",
                          Args {}.with ("owner", "Kick").with ("param", "Cutoff"), "en")
           == "Kick > Cutoff");

    // A message with no placeholders is its own answer.
    CHECK (formatMessage ("Delete pattern", {}, "en") == "Delete pattern");
}

TEST_CASE ("an argument nobody supplied stays visible", "[i18n]")
{
    // Not dropped, and not blanked. A brace on screen is findable; a silently
    // empty word is what somebody ships. The gate below is what stops one
    // reaching a release, and it can only do that if this is the behaviour.
    CHECK (formatMessage ("Reset {param}", {}, "en").contains ("{param}"));
}

TEST_CASE ("a plural picks its branch by category", "[i18n]")
{
    const juce::String notes { "{count, plural, one {# note} other {# notes}}" };

    CHECK (formatMessage (notes, Args {}.count (1), "en") == "1 note");
    CHECK (formatMessage (notes, Args {}.count (0), "en") == "0 notes");
    CHECK (formatMessage (notes, Args {}.count (12), "en") == "12 notes");

    // The branch a message does not carry falls back to 'other', so a two-form
    // message stays a real sentence in a four-form locale.
    CHECK (formatMessage (notes, Args {}.count (3), "cs") == "3 notes");
}

TEST_CASE ("a plural rule is the language's, not the region's", "[i18n]")
{
    CHECK (pluralFor ("en", 1) == PluralCategory::one);
    CHECK (pluralFor ("en", 2) == PluralCategory::other);

    // French counts zero as singular; English does not. This is the difference
    // a hand-rolled `n == 1 ? "" : "s"` cannot express.
    CHECK (pluralFor ("fr", 0) == PluralCategory::one);
    CHECK (pluralFor ("en", 0) == PluralCategory::other);

    // Czech has a 'few', and fr-CA counts as fr does.
    CHECK (pluralFor ("cs", 3) == PluralCategory::few);
    CHECK (pluralFor ("cs", 5) == PluralCategory::other);
    CHECK (pluralFor ("fr-CA", 0) == PluralCategory::one);

    // An unknown language gets English's rule rather than no rule.
    CHECK (pluralFor ("xx", 1) == PluralCategory::one);
}

TEST_CASE ("a select picks its branch by value", "[i18n]")
{
    const juce::String state { "{kind, select, wav {WAV} flac {FLAC} other {audio}}" };

    CHECK (formatMessage (state, Args {}.with ("kind", "wav"), "en") == "WAV");
    CHECK (formatMessage (state, Args {}.with ("kind", "mp3"), "en") == "audio");
}

TEST_CASE ("a quoted brace is a brace", "[i18n]")
{
    CHECK (formatMessage ("'{'name'}'", {}, "en") == "{name}");
    CHECK (formatMessage ("don't stop", {}, "en") == "don't stop");
}

TEST_CASE ("a malformed message is returned rather than dropped", "[i18n]")
{
    CHECK_FALSE (isWellFormedMessage ("Reset {param"));
    CHECK_FALSE (isWellFormedMessage ("{count, plural, one {# note}}")); // no 'other'
    CHECK_FALSE (isWellFormedMessage ("{count, selectordinal, other {#}}"));
    CHECK (isWellFormedMessage ("{count, plural, one {# note} other {# notes}}"));
    CHECK (isWellFormedMessage ("Delete pattern"));

    CHECK (formatMessage ("Reset {param", {}, "en").contains ("{param"));
}

TEST_CASE ("a locale negotiates down to what was compiled in", "[i18n]")
{
    REQUIRE (availableLocales().contains ("en"));

    setLocale ("en");
    CHECK (activeLocale() == "en");

    // A region falls back to its language, and an unknown tag to the reference,
    // rather than to a blank catalogue.
    setLocale ("de-CH");
    CHECK (availableLocales().contains (activeLocale()));

    setLocale ("en");
}

TEST_CASE ("every string in the catalogue says something", "[i18n][gate]")
{
    // The rule the two coverage gates depend on. HoverHelpTests asks every
    // control in the window what it is, and AccessibilityTests asks that a
    // tooltip and an accessible name are the same sentence - both read a
    // string and treat blank as silence. A tr() that could return "" turns
    // both of them into tests that pass over an empty answer.
    //
    // Control case: a walk over nothing is not a walk.
    REQUIRE (numStrings > 10);

    juce::StringArray blank;

    for (auto i = 0; i < numStrings; ++i)
    {
        const auto id = (StringId) i;

        if (tr (id).isEmpty())
            blank.add (keyOf (id));
    }

    INFO ("keys that resolve to nothing:\n" << blank.joinIntoString ("\n"));
    CHECK (blank.isEmpty());
}

TEST_CASE ("every message is answerable with the arguments it names", "[i18n][gate]")
{
    // The failure this exists for is a brace on screen. tr() leaves a
    // placeholder it was not given, and it does so in the one state nobody
    // screenshots. The generator recorded every argument every message asks
    // for, so this can answer all of them without knowing what any of them say.
    REQUIRE (numStrings > 10);

    juce::StringArray unresolved;
    juce::StringArray malformed;

    for (auto i = 0; i < numStrings; ++i)
    {
        const auto id = (StringId) i;

        if (! isWellFormedMessage (tr (id)))
            malformed.add (keyOf (id) + "  " + tr (id));

        Args supplied;

        for (const auto& name : argumentNamesOf (id))
            supplied.with (name.toRawUTF8(), (juce::int64) 1);

        if (tr (id, supplied).containsChar ('{'))
            unresolved.add (keyOf (id) + "  ->  " + tr (id, supplied));
    }

    INFO ("messages the formatter does not understand:\n" << malformed.joinIntoString ("\n"));
    CHECK (malformed.isEmpty());

    INFO ("braces that reach the screen:\n" << unresolved.joinIntoString ("\n"));
    CHECK (unresolved.isEmpty());

    // Control case: the gate has to be able to SEE an unanswered placeholder.
    CHECK (formatMessage ("Reset {param}", {}, "en").containsChar ('{'));
    CHECK_FALSE (
        formatMessage ("Reset {param}", Args {}.with ("param", "Cutoff"), "en").containsChar ('{'));
}

TEST_CASE ("the catalogue holds no non-ASCII decoded as ASCII", "[i18n][gate]")
{
    // juce::String's const char* constructor decodes ASCII and mangles every
    // multi-byte character; operator+= decodes UTF-8. Strings.cpp goes through
    // CharPointer_UTF8 for exactly this reason, and a regression there shows up
    // as 0xc2 in front of every punctuation mark the catalogue holds.
    REQUIRE (numStrings > 10);

    juce::StringArray mangled;

    for (auto i = 0; i < numStrings; ++i)
    {
        const auto id = (StringId) i;

        if (tr (id).containsChar ((juce::juce_wchar) 0xc2))
            mangled.add (keyOf (id));
    }

    INFO ("UTF-8 decoded as ASCII:\n" << mangled.joinIntoString ("\n"));
    CHECK (mangled.isEmpty());

    // Control case: the catalogue must actually HOLD a non-ASCII character, or
    // this gate is a walk over nothing but plain words.
    auto nonAscii = false;

    for (auto i = 0; i < numStrings; ++i)
        for (auto c : tr ((StringId) i))
            if (c > 127)
                nonAscii = true;

    CHECK (nonAscii);
}
