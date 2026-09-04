#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "lang/Diagnostics.h"
#include "lang/Messages.h"

#include "MessageFormatCases.h"

using namespace dew::lang;

namespace
{

MsgArgs argumentsFor (const dew::testing::MessageCase& c)
{
    MsgArgs arguments;

    if (c.name != nullptr)
        arguments.with (c.name, c.value);

    if (c.hasCount)
        arguments.count (c.count);

    return arguments;
}

} // namespace

TEST_CASE ("the score language formats the subset the application does", "[score][i18n]")
{
    // Two implementations of one subset, one table. src/lang/MessageFormat.h
    // says why there are two; tests/MessageFormatCases.h says why the cases are
    // shared. dew_tests runs the same table through the juce::String one, so a
    // fix applied to one loop and not the other fails there and not here.
    for (const auto& c : dew::testing::messageCases)
    {
        INFO (c.message << "  in " << c.locale);
        CHECK (formatMessage (c.message, argumentsFor (c), c.locale) == c.expected);
    }
}

TEST_CASE ("the generated plural rules say what plurals.txt says", "[score][i18n]")
{
    for (const auto& c : dew::testing::pluralCases)
    {
        INFO (c.locale << "  " << c.count);
        CHECK (
            std::string (dew::plural::nameOfCategory (dew::plural::categoryFor (c.locale, c.count)))
            == c.category);
    }
}

TEST_CASE ("a message the catalogue does not hold answers with its key", "[score][i18n]")
{
    // Never empty, for the reason a missing tr() key is never empty: a
    // diagnostic that says nothing reads as a compiler that found something and
    // would not say what. The key is greppable; a blank line is not.
    const auto beyond = (Msg) numMessages;

    CHECK (msg (beyond).empty());
    CHECK (messageKey (beyond).empty());

    // And every id the enum DOES hold says something.
    for (auto i = 0; i < numMessages; ++i)
    {
        INFO (messageKey ((Msg) i));
        CHECK_FALSE (msg ((Msg) i).empty());
    }
}

TEST_CASE ("every score message is answerable with the arguments it names", "[score][i18n][gate]")
{
    // The generator records what each message asks for, so this can answer all
    // of them without knowing what any of them say. A brace left in a rendered
    // diagnostic is a placeholder nobody bound, and this is what refuses one.
    for (auto i = 0; i < numMessages; ++i)
    {
        const auto id = (Msg) i;
        const auto names = messageArgumentNames (id);

        MsgArgs arguments;

        for (std::size_t start = 0; start < names.size();)
        {
            auto end = names.find (' ', start);

            if (end == std::string_view::npos)
                end = names.size();

            // A count is bound as a number, because a plural rule that is
            // handed "count" as text would pick 'other' for every locale and
            // this gate would pass on a message it never really answered.
            const std::string name { names.substr (start, end - start) };

            if (name == "count")
                arguments.count (2);
            else
                arguments.with (name.c_str(), "x");

            start = end + 1;
        }

        const auto rendered = msg (id, arguments);

        INFO (messageKey (id) << "  ->  " << rendered);
        CHECK_FALSE (dew::testing::holdsAPlaceholder (rendered.c_str()));
        CHECK (isWellFormedMessage (messageText (id)));
    }

    // Control case: the gate has to be able to SEE an unanswered placeholder,
    // and has to leave a quoted brace alone.
    CHECK (dew::testing::holdsAPlaceholder (formatMessage ("Reset {param}", {}, "en").c_str()));
    CHECK_FALSE (dew::testing::holdsAPlaceholder (
        formatMessage ("try `channel lead '{'`", {}, "en").c_str()));
}

TEST_CASE ("a locale is chosen by tag and then by language", "[score][i18n]")
{
    // Written against however many catalogues this build carries, not against
    // one: an assertion that `de` falls back to the reference is true only
    // while German is not compiled in, and it is exactly the assertion that
    // goes quietly wrong the day it is.
    for (auto i = 0; i < numLocales; ++i)
    {
        const auto locale = (Locale) i;
        const std::string tag { localeTag (locale) };

        INFO ("locale: " << tag);
        CHECK (localeFor (tag) == locale);

        // A regional tag reaches its language: fr-CA compiles its errors in
        // French rather than falling all the way back to English.
        CHECK (localeFor (tag + "-XX") == locale);
    }

    // A tag no catalogue claims falls back to the reference rather than to
    // nothing. `zz` is not a language.
    CHECK (localeFor ("zz") == referenceLocale);
    CHECK (localeFor ("") == referenceLocale);
}

TEST_CASE ("a diagnostic is written in its bag's locale", "[score][i18n]")
{
    // The locale is a constructor argument and nothing else holds one. Two bags
    // over the same source can therefore disagree about language and about
    // nothing else, which is what makes a compile deterministic in a program
    // that lets a person change languages.
    DiagnosticBag defaulted { "" };
    CHECK (defaulted.locale() == referenceLocale);

    DiagnosticBag chosen { "", localeFor ("en") };
    CHECK (chosen.text (Msg::generator_tooManyChannels_message) == "too many channels");

    CHECK (chosen.text (Msg::generator_tooManyChannels_note, MsgArgs {}.count (1))
           == "dew plays at most 1 channel");
    CHECK (chosen.text (Msg::generator_tooManyChannels_note, MsgArgs {}.count (16))
           == "dew plays at most 16 channels");
}

TEST_CASE ("no two messages share a key", "[score][i18n][gate]")
{
    // The enum is generated from the key set, so this cannot fail by accident -
    // it fails if the mangling ever stops being injective, which is what the
    // ban on underscores in a key segment exists to guarantee.
    std::set<std::string_view> seen;

    for (auto i = 0; i < numMessages; ++i)
        CHECK (seen.insert (messageKey ((Msg) i)).second);

    CHECK ((int) seen.size() == numMessages);
}
