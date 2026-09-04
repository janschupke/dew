#include "lang/MessageFormat.h"

#include <cstddef>

namespace dew::lang
{

namespace
{

constexpr auto npos = std::string_view::npos;

bool isSpace (char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/** The index just past the '}' that closes the '{' at `open`, or npos.

    Depth-counted rather than found by searching for '}', because every plural
    message nests: the branches of "{count, plural, one {# bar} other {# bars}}"
    carry braces of their own and the first '}' closes none of them.
*/
std::size_t endOfBraced (std::string_view text, std::size_t open) noexcept
{
    auto depth = 0;

    for (auto i = open; i < text.size(); ++i)
    {
        if (text[i] == '{')
        {
            ++depth;
        }
        else if (text[i] == '}')
        {
            if (--depth == 0)
                return i + 1;
        }
    }

    return npos;
}

/** The branch named `wanted` inside a plural or select body, or the one named
    "other". False when the body names neither, which is a malformed message
    rather than an empty result. */
bool branchFor (std::string_view body, std::string_view wanted, std::string_view& result)
{
    std::string_view fallback;
    auto found = false;
    std::size_t i = 0;

    while (i < body.size())
    {
        while (i < body.size() && isSpace (body[i]))
            ++i;

        const auto nameStart = i;

        while (i < body.size() && body[i] != '{' && ! isSpace (body[i]))
            ++i;

        const auto name = body.substr (nameStart, i - nameStart);

        while (i < body.size() && isSpace (body[i]))
            ++i;

        if (i >= body.size() || body[i] != '{')
            break;

        const auto close = endOfBraced (body, i);

        if (close == npos)
            break;

        const auto branch = body.substr (i + 1, close - i - 2);
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
    std::string_view name;
    std::string_view keyword; ///< "plural", "select", or empty for a substitution
    std::string_view body;
};

std::string_view trimmed (std::string_view text) noexcept
{
    while (! text.empty() && isSpace (text.front()))
        text.remove_prefix (1);

    while (! text.empty() && isSpace (text.back()))
        text.remove_suffix (1);

    return text;
}

/** Split "{count, plural, one {...} other {...}}"'s interior into its parts. */
Placeholder parsePlaceholder (std::string_view interior)
{
    Placeholder placeholder;

    const auto firstComma = interior.find (',');

    if (firstComma == npos)
    {
        placeholder.name = trimmed (interior);
        return placeholder;
    }

    placeholder.name = trimmed (interior.substr (0, firstComma));

    const auto rest = interior.substr (firstComma + 1);
    const auto secondComma = rest.find (',');

    if (secondComma == npos)
    {
        placeholder.keyword = trimmed (rest);
        return placeholder;
    }

    placeholder.keyword = trimmed (rest.substr (0, secondComma));
    placeholder.body = rest.substr (secondComma + 1);

    return placeholder;
}

/** One pass over `message`. `count` is what '#' means here: absent at the top
    level, bound to the argument inside a plural branch. */
std::string render (std::string_view message, const MsgArgs& arguments, std::string_view locale,
                    const MsgArg* count)
{
    std::string out;

    for (std::size_t i = 0; i < message.size();)
    {
        const auto c = message[i];

        // ICU's quoting. '{' is a literal brace; '' is a literal apostrophe;
        // an apostrophe before anything else is just an apostrophe, which is
        // what keeps "don't" from opening a quoted run.
        if (c == '\'' && i + 1 < message.size())
        {
            const auto next = message[i + 1];

            if (next == '\'')
            {
                out += '\'';
                i += 2;
                continue;
            }

            if (next == '{' || next == '}' || next == '#')
            {
                const auto closing = message.find ('\'', i + 2);
                const auto end = closing == npos ? message.size() : closing;

                out += message.substr (i + 1, end - i - 1);
                i = closing == npos ? message.size() : closing + 1;
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
            out += c;
            ++i;
            continue;
        }

        const auto close = endOfBraced (message, i);

        if (close == npos)
        {
            // Unbalanced. Copy the rest through rather than dropping it: the
            // brace in the diagnostic is what makes the defect findable.
            out += message.substr (i);
            break;
        }

        const auto placeholder = parsePlaceholder (message.substr (i + 1, close - i - 2));
        const auto* argument = arguments.find (placeholder.name);

        if (argument == nullptr)
        {
            // An argument nobody supplied. The placeholder stays as written -
            // see the gate "every message is answerable with the arguments it
            // names", which is what stops this reaching a screen.
            out += message.substr (i, close - i);
            i = close;
            continue;
        }

        if (placeholder.keyword.empty())
        {
            out += argument->toDisplayString();
        }
        else if (placeholder.keyword == "plural")
        {
            const auto category = plural::categoryFor (locale, argument->asInteger());
            std::string_view branch;

            if (branchFor (placeholder.body, plural::nameOfCategory (category), branch))
                out += render (branch, arguments, locale, argument);
            else
                out += message.substr (i, close - i);
        }
        else if (placeholder.keyword == "select")
        {
            std::string_view branch;

            if (branchFor (placeholder.body, argument->toDisplayString(), branch))
                out += render (branch, arguments, locale, count);
            else
                out += message.substr (i, close - i);
        }
        else
        {
            out += message.substr (i, close - i);
        }

        i = close;
    }

    return out;
}

} // namespace

std::string formatMessage (std::string_view message, const MsgArgs& arguments,
                           std::string_view locale)
{
    return render (message, arguments, locale, nullptr);
}

bool isWellFormedMessage (std::string_view message)
{
    for (std::size_t i = 0; i < message.size(); ++i)
    {
        const auto c = message[i];

        if (c == '\'' && i + 1 < message.size()
            && (message[i + 1] == '{' || message[i + 1] == '}' || message[i + 1] == '#'))
        {
            const auto closing = message.find ('\'', i + 2);
            i = closing == npos ? message.size() : closing;
            continue;
        }

        if (c != '{')
            continue;

        const auto close = endOfBraced (message, i);

        if (close == npos)
            return false;

        const auto placeholder = parsePlaceholder (message.substr (i + 1, close - i - 2));

        if (placeholder.name.empty())
            return false;

        // A keyword this formatter does not implement is malformed here rather
        // than silently printed. 'choice' and 'selectordinal' are ICU's and
        // deliberately not dew's.
        if (! placeholder.keyword.empty() && placeholder.keyword != "plural"
            && placeholder.keyword != "select")
            return false;

        // Every plural and select must offer a branch that always matches.
        if (! placeholder.keyword.empty())
        {
            std::string_view branch;

            if (! branchFor (placeholder.body, "other", branch))
                return false;
        }
    }

    return true;
}

} // namespace dew::lang
