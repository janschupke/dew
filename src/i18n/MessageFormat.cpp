#include "i18n/MessageFormat.h"

#include "i18n/PluralRules.h"

namespace dew
{

namespace
{

/** The index just past the '}' that closes the '{' at `open`, or -1.

    Depth-counted rather than found with indexOf, because every plural message
    nests: the branches of "{count, plural, one {# bar} other {# bars}}" carry
    braces of their own and the first '}' in the string closes none of them.
*/
int endOfBraced (const juce::String& text, int open)
{
    auto depth = 0;

    for (auto i = open; i < text.length(); ++i)
    {
        const auto c = text[i];

        if (c == '{')
        {
            ++depth;
        }
        else if (c == '}')
        {
            if (--depth == 0)
                return i + 1;
        }
    }

    return -1;
}

/** The branch named `wanted` inside a plural or select body, or the one named
    "other". Returns false when the body names neither, which is a malformed
    message rather than an empty result. */
bool branchFor (const juce::String& body, juce::StringRef wanted, juce::String& result)
{
    juce::String fallback;
    auto found = false;
    auto i = 0;

    while (i < body.length())
    {
        while (i < body.length() && juce::CharacterFunctions::isWhitespace (body[i]))
            ++i;

        const auto nameStart = i;

        while (i < body.length() && body[i] != '{'
               && ! juce::CharacterFunctions::isWhitespace (body[i]))
            ++i;

        const auto name = body.substring (nameStart, i);

        while (i < body.length() && juce::CharacterFunctions::isWhitespace (body[i]))
            ++i;

        if (i >= body.length() || body[i] != '{')
            break;

        const auto close = endOfBraced (body, i);

        if (close < 0)
            break;

        const auto branch = body.substring (i + 1, close - 1);
        i = close;

        if (name == wanted)
        {
            result = branch;
            return true;
        }

        if (name == "other")
        {
            fallback = branch;
            found = true;
        }
    }

    if (found)
        result = fallback;

    return found;
}

struct Placeholder
{
    juce::String name;
    juce::String keyword; ///< "plural", "select", or empty for a substitution
    juce::String body;
};

/** Split "{count, plural, one {...} other {...}}"'s interior into its parts. */
Placeholder parsePlaceholder (const juce::String& interior)
{
    Placeholder placeholder;

    const auto firstComma = interior.indexOfChar (',');

    if (firstComma < 0)
    {
        placeholder.name = interior.trim();
        return placeholder;
    }

    placeholder.name = interior.substring (0, firstComma).trim();

    const auto rest = interior.substring (firstComma + 1);
    const auto secondComma = rest.indexOfChar (',');

    if (secondComma < 0)
    {
        placeholder.keyword = rest.trim();
        return placeholder;
    }

    placeholder.keyword = rest.substring (0, secondComma).trim();
    placeholder.body = rest.substring (secondComma + 1);

    return placeholder;
}

/** One pass over `message`. `count` is what '#' means here: absent at the top
    level, bound to the argument inside a plural branch. */
juce::String render (const juce::String& message, const Args& arguments, juce::StringRef locale,
                     const Arg* count)
{
    juce::String out;

    for (auto i = 0; i < message.length();)
    {
        const auto c = message[i];

        // ICU's quoting. '{' is a literal brace; '' is a literal apostrophe;
        // an apostrophe before anything else is just an apostrophe, which is
        // what keeps "don't" from opening a quoted run.
        if (c == '\'' && i + 1 < message.length())
        {
            const auto next = message[i + 1];

            if (next == '\'')
            {
                out += "'";
                i += 2;
                continue;
            }

            if (next == '{' || next == '}' || next == '#')
            {
                const auto closing = message.indexOfChar (i + 2, '\'');
                const auto end = closing < 0 ? message.length() : closing;

                out += message.substring (i + 1, end);
                i = closing < 0 ? message.length() : closing + 1;
                continue;
            }
        }

        if (c == '#' && count != nullptr)
        {
            out += count->toDisplayString();
            ++i;
            continue;
        }

        if (c != '{')
        {
            out += juce::String::charToString (c);
            ++i;
            continue;
        }

        const auto close = endOfBraced (message, i);

        if (close < 0)
        {
            // Unbalanced. Copy the rest through rather than dropping it: the
            // brace on screen is what makes the defect findable.
            out += message.substring (i);
            break;
        }

        const auto placeholder = parsePlaceholder (message.substring (i + 1, close - 1));
        const auto* argument = arguments.find (placeholder.name);

        if (argument == nullptr)
        {
            // An argument nobody supplied. The placeholder stays as written -
            // see the gate "every message is answerable with the arguments it
            // names", which is what stops this reaching a screen.
            out += message.substring (i, close);
            i = close;
            continue;
        }

        if (placeholder.keyword.isEmpty())
        {
            out += argument->toDisplayString();
        }
        else if (placeholder.keyword == "plural")
        {
            const auto category = pluralFor (locale, argument->asInteger());
            juce::String branch;

            if (branchFor (placeholder.body, nameOfPluralCategory (category), branch))
                out += render (branch, arguments, locale, argument);
            else
                out += message.substring (i, close);
        }
        else if (placeholder.keyword == "select")
        {
            juce::String branch;

            if (branchFor (placeholder.body, argument->toDisplayString(), branch))
                out += render (branch, arguments, locale, count);
            else
                out += message.substring (i, close);
        }
        else
        {
            out += message.substring (i, close);
        }

        i = close;
    }

    return out;
}

} // namespace

juce::String formatMessage (juce::StringRef message, const Args& arguments, juce::StringRef locale)
{
    return render (juce::String (message), arguments, locale, nullptr);
}

bool isWellFormedMessage (juce::StringRef message)
{
    const juce::String text (message);
    auto depth = 0;

    for (auto i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];

        if (c == '\'' && i + 1 < text.length()
            && (text[i + 1] == '{' || text[i + 1] == '}' || text[i + 1] == '#'))
        {
            const auto closing = text.indexOfChar (i + 2, '\'');
            i = closing < 0 ? text.length() : closing;
            continue;
        }

        if (c == '{')
        {
            const auto close = endOfBraced (text, i);

            if (close < 0)
                return false;

            const auto placeholder = parsePlaceholder (text.substring (i + 1, close - 1));

            if (placeholder.name.isEmpty())
                return false;

            // A keyword this formatter does not implement is malformed here
            // rather than silently printed. 'choice' and 'selectordinal' are
            // ICU's and deliberately not dew's.
            if (placeholder.keyword.isNotEmpty() && placeholder.keyword != "plural"
                && placeholder.keyword != "select")
                return false;

            // Every plural and select must offer a branch that always matches.
            if (placeholder.keyword.isNotEmpty())
            {
                juce::String branch;

                if (! branchFor (placeholder.body, "other", branch))
                    return false;
            }

            ++depth;
        }
    }

    return depth >= 0;
}

} // namespace dew
