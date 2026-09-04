#pragma once

#include <cstdint>

/** The formatter cases both message formatters are held to.

    dew has two implementations of the same ICU subset - i18n/MessageFormat.cpp
    over juce::String and lang/MessageFormat.cpp over std::string - because
    dew_lang links nothing at all and dew_i18n links juce_core, so neither can
    call the other. src/lang/MessageFormat.h argues why that duplication is the
    lesser evil.

    What makes it survivable is that neither is checked against its own idea of
    correct. The table below is the subject, the two suites are two harnesses
    over it, and a fix applied to one loop and not the other fails in the suite
    that did not get it. The RULES are not here at all: those are generated into
    both libraries from resources/i18n/plurals.txt, so a plural rule cannot
    diverge in the first place.

    Deliberately free of juce and of lang: a header both a JUCE test and a
    JUCE-free one include has to be, and tests/ is one directory with no layer
    above or below it.
*/
namespace dew::testing
{

/** One case: a message, a locale, at most one text argument and at most one
    count, and what the formatter must produce.

    Two arguments is the widest case in the table and the widest shape both
    Args types offer in the same spelling; a case needing more would be a case
    about one implementation rather than about the subset.
*/
struct MessageCase
{
    const char* message;
    const char* locale;
    const char* name;  ///< a text argument's name, or nullptr for none
    const char* value; ///< that argument's value
    bool hasCount;     ///< whether `count` is bound, as "count"
    std::int64_t count;
    const char* expected;
};

// clang-format off
inline constexpr MessageCase messageCases[] {
    // Substitution.
    { "Reset {param}",            "en", "param", "Cutoff", false, 0, "Reset Cutoff" },
    { "Delete pattern",           "en", nullptr, nullptr,  false, 0, "Delete pattern" },

    // An argument nobody supplied stays visible. Not dropped, and not blanked:
    // a brace in the output is findable, a silently empty word is what ships.
    { "Reset {param}",            "en", nullptr, nullptr,  false, 0, "Reset {param}" },

    // Plurals, and the whole reason an integer stays an integer.
    { "{count, plural, one {# note} other {# notes}}", "en", nullptr, nullptr, true, 1,  "1 note" },
    { "{count, plural, one {# note} other {# notes}}", "en", nullptr, nullptr, true, 0,  "0 notes" },
    { "{count, plural, one {# note} other {# notes}}", "en", nullptr, nullptr, true, 12, "12 notes" },

    // The branch a message does not carry falls back to 'other', so a two-form
    // message stays a real sentence in a four-form locale.
    { "{count, plural, one {# note} other {# notes}}", "cs", nullptr, nullptr, true, 3,  "3 notes" },

    // French counts zero as singular; English does not.
    { "{count, plural, one {# note} other {# notes}}", "fr", nullptr, nullptr, true, 0,  "0 note" },

    // A plural branch is itself a message: it substitutes, and '#' is the count.
    { "{count, plural, one {# bar of {name}} other {# bars of {name}}}",
      "en", "name", "verse", true, 2, "2 bars of verse" },

    // Select, by the value's own spelling.
    { "{kind, select, wav {WAV} other {audio}}", "en", "kind", "wav",  false, 0, "WAV" },
    { "{kind, select, wav {WAV} other {audio}}", "en", "kind", "aiff", false, 0, "audio" },

    // ICU quoting. A quoted brace is a brace; a lone apostrophe is an
    // apostrophe, which is what keeps "don't" from opening a quoted run.
    { "a '{' is a brace",  "en", nullptr, nullptr, false, 0, "a { is a brace" },
    { "don't stop",        "en", nullptr, nullptr, false, 0, "don't stop" },
    { "it''s here",        "en", nullptr, nullptr, false, 0, "it's here" },

    // Malformed is returned rather than dropped. The visible defect is the
    // point: a blank label is what nobody notices.
    { "unbalanced {param",  "en", "param", "x", false, 0, "unbalanced {param" },
    { "{count, plural, one {# note}}", "en", nullptr, nullptr, true, 5,
      "{count, plural, one {# note}}" },
};

/** The category a count takes in a locale, as the generated rules must answer.

    The rules are emitted into both libraries from one file, so this is not
    checking two transcriptions against each other - it is checking that the
    generator turned plurals.txt into what plurals.txt says.
*/
struct PluralCase
{
    const char* locale;
    std::int64_t count;
    const char* category;
};

inline constexpr PluralCase pluralCases[] {
    { "en",    1,   "one"   },
    { "en",    0,   "other" },
    { "en",    2,   "other" },

    // French counts zero as singular. This is the difference a hand-rolled
    // `n == 1 ? "" : "s"` cannot express.
    { "fr",    0,   "one"   },
    { "fr",    1,   "one"   },
    { "fr",    2,   "other" },

    // Czech has a 'few', and fr-CA counts as fr does: a region never changes
    // how a number is counted.
    { "cs",    1,   "one"   },
    { "cs",    3,   "few"   },
    { "cs",    5,   "other" },
    { "sk",    4,   "few"   },
    { "fr-CA", 0,   "one"   },
    { "cs-CZ", 3,   "few"   },
    { "EN-GB", 1,   "one"   },

    // Polish, which is the one rule in the file with a modulus and the one
    // whose fallback is 'many' rather than 'other'.
    { "pl",    1,   "one"   },
    { "pl",    2,   "few"   },
    { "pl",    22,  "few"   },
    { "pl",    12,  "many"  },
    { "pl",    112, "many"  },
    { "pl",    5,   "many"  },

    // Languages with no grammatical plural at all.
    { "ja",    1,   "other" },
    { "zh",    5,   "other" },

    // A language the file names nowhere gets the two-form default rather than
    // no rule: every message declares 'other', so an unknown locale reads a
    // real sentence rather than a missing branch.
    { "xx",    1,   "one"   },
    { "xx",    7,   "other" },

    // A negative count is counted by its magnitude.
    { "en",    -1,  "one"   },
    { "cs",    -3,  "few"   },
};
// clang-format on

} // namespace dew::testing
