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

TEST_CASE ("the catalogue is decoded as UTF-8, not as ASCII", "[i18n][gate]")
{
    // juce::String's const char* constructor decodes ASCII and mangles every
    // multi-byte character; operator+= decodes UTF-8. Strings.cpp goes through
    // CharPointer_UTF8 for exactly that reason, and getting it wrong shows up
    // as 0xc2 in front of every punctuation mark a locale holds.
    REQUIRE (numStrings > 10);

    juce::StringArray mangled;

    for (auto i = 0; i < numStrings; ++i)
        if (tr ((StringId) i).containsChar ((juce::juce_wchar) 0xc2))
            mangled.add (keyOf ((StringId) i));

    INFO ("UTF-8 decoded as ASCII:\n" << mangled.joinIntoString ("\n"));
    CHECK (mangled.isEmpty());

    // Control case, and it deliberately does NOT ask the catalogue for a
    // non-ASCII string. en.json is pure ASCII today - a catalogue in English
    // reasonably might be - so a walk over it proves the decoder works only for
    // as long as somebody happens to have written a dash in it. These are the
    // two spellings the gate at SourceGateTests exists for, on bytes of our
    // own: a middle dot decoded correctly, and the same bytes through the
    // constructor that gets it wrong.
    //
    // A middle dot rather than the em dash that gate's comment quotes, because
    // 0xc2 is the lead byte only for U+0080 to U+00BF. An em dash begins 0xe2
    // and mangles just as thoroughly while showing none of the character this
    // test looks for - worth knowing before reading the check above as broader
    // than it is.
    const char* const middleDot = "\xc2\xb7";

    const juce::String correct { juce::CharPointer_UTF8 (middleDot) };
    const juce::String wrong { middleDot };

    CHECK (correct.length() == 1);
    CHECK_FALSE (correct.containsChar ((juce::juce_wchar) 0xc2));

    CHECK (wrong.length() == 2);
    CHECK (wrong.containsChar ((juce::juce_wchar) 0xc2));

    // And a message formatted from a non-ASCII argument keeps it intact, which
    // is the path every dialog body takes.
    CHECK (
        formatMessage ("Delete {name}", Args {}.with ("name", correct), "en").contains (correct));
}

TEST_CASE ("the extracted plurals still read as they did", "[i18n]")
{
    // Ten sites in the tree spelled a plural as n == 1 ? "note" : "notes". The
    // catalogue answers them now, and this pass ships English only - so every
    // one of them has to produce the string it replaced, in both branches.
    //
    // Written out rather than looped, because the point is the SPELLING and a
    // loop over the catalogue would only prove the formatter agrees with
    // itself.
    setLocale ("en");

    CHECK (tr (StringId::status_notes, Args {}.count (1)) == "1 note");
    CHECK (tr (StringId::status_notes, Args {}.count (4)) == "4 notes");

    CHECK (tr (StringId::status_dropouts, Args {}.count (1)) == "1 drop");
    CHECK (tr (StringId::status_dropouts, Args {}.count (2)) == "2 drops");

    CHECK (tr (StringId::status_loadWarnings, Args {}.count (1).with ("first", "a clip"))
           == "1 item in this file were not understood: a clip");

    CHECK (tr (StringId::score_errors, Args {}.count (1))
           == "Score has 1 error - nothing was written");
    CHECK (tr (StringId::score_errors, Args {}.count (9))
           == "Score has 9 errors - nothing was written");

    CHECK (tr (StringId::score_compiled,
               Args {}.with ("patterns", 1).with ("clips", 1).with ("notes", 1))
           == "Score compiled: 1 pattern, 1 clip, 1 note");
    CHECK (tr (StringId::score_compiled,
               Args {}.with ("patterns", 3).with ("clips", 5).with ("notes", 128))
           == "Score compiled: 3 patterns, 5 clips, 128 notes");

    CHECK (tr (StringId::score_keptByHand, Args {}.count (1))
           == " - 1 pattern was edited by hand and left alone");
    CHECK (tr (StringId::score_keptByHand, Args {}.count (2))
           == " - 2 patterns were edited by hand and left alone");
}

TEST_CASE ("a confirmation names what it is about to destroy", "[i18n]")
{
    // The four destructive dialogs interpolate a name that a person typed, and
    // they used to build the sentence around it with +. The quoting is part of
    // the sentence and therefore part of the translation, which is the whole
    // reason it is a placeholder rather than three fragments.
    setLocale ("en");

    CHECK (tr (StringId::dialog_deletePattern_body, Args {}.with ("name", "Groove"))
           == "Delete \"Groove\"? Every clip that plays it goes with it.");

    CHECK (tr (StringId::dialog_removeChannel_body, Args {}.with ("name", "Kick"))
           == "Remove \"Kick\"? Its notes in every pattern go with it.");

    CHECK (tr (StringId::dialog_removeTrack_body, Args {}.with ("name", "Drums"))
           == "Remove \"Drums\"? Every clip on it goes with it.");
}

TEST_CASE ("a language is offered in its own language", "[i18n]")
{
    // A picker shows every language in its own language: a German reader
    // looking for German looks for "Deutsch". So these are not catalogue keys,
    // and this test is what says they are still right.
    CHECK (endonymOf ("en") == "English");
    CHECK (endonymOf ("de") == "Deutsch");

    // The region falls back to the language, the way plural rules do.
    CHECK (endonymOf ("de-AT") == "Deutsch");

    // Non-ASCII survives the trip. These are the only non-ASCII literals in
    // dew's own source, and they go through CharPointer_UTF8 - so a regression
    // to juce::String's other constructor shows up as the 0xc2 the gate at
    // SourceGateTests exists for.
    CHECK_FALSE (endonymOf ("fr").containsChar ((juce::juce_wchar) 0xc2));
    CHECK (endonymOf ("fr").length() == 8);
    CHECK (endonymOf ("cs").length() == 7);

    // An unknown tag answers with the tag: wrong, but visible.
    CHECK (endonymOf ("xx") == "xx");
}
