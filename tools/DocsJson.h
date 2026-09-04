#pragma once

#include <cmath>
#include <type_traits>
#include <string>
#include <string_view>
#include <vector>

namespace dew::docs
{

/** A JSON writer that spells everything out, because the output is diffed.

    juce::JSON is available to dew_shot and is deliberately not used by either
    emitter. Its key order comes from a NamedValueSet, its indentation is its
    own and its number formatting is its own - all three are JUCE's to change,
    and a JUCE bump that changed any of them would turn the freshness gate red
    for a reason with nothing to do with the change being made. That is the
    same failure THIRD_PARTY.md's missing generation date exists to refuse.

    It is also free of JUCE entirely, which is what lets dew_docs link dew_lang
    and nothing else and so prove that library's zero-dependency claim a second
    time.

    Members come out in the order they are written. There is no map anywhere in
    this file, and no sort: a sort over std::string_view is one careless `<`
    away from comparing POINTERS, which would order the output by wherever the
    linker happened to put the literals.
*/
class JsonWriter
{
public:
    void beginObject()
    {
        open ('{');
    }

    void endObject()
    {
        close ('}');
    }

    void beginArray()
    {
        open ('[');
    }

    void endArray()
    {
        close (']');
    }

    /** A member name. Every value call below is preceded by one of these,
        except inside an array.
    */
    void key (std::string_view name)
    {
        separate();
        text += '"';
        escapeInto (name);
        text += "\": ";
        pending = true;
    }

    void value (std::string_view v)
    {
        separate();
        text += '"';
        escapeInto (v);
        text += '"';
    }

    /** A string literal.

        This overload is load-bearing, not a convenience. `const char*` converts
        to `bool` by a STANDARD conversion and to `std::string_view` only by a
        user-defined one, so without it every `nameOf()` in the tree binds to
        the bool overload below and the schema comes out as `"name": true`. It
        did, on the first run. An exact match beats both conversions.
    */
    void value (const char* v)
    {
        value (std::string_view (v));
    }

    /** True or false, and nothing that merely converts to one.

        Constrained for the same reason the overload above exists: an unrelated
        pointer or an int reaching this by conversion is a value silently
        written as `true`, which is valid JSON and is not what anybody meant.
    */
    template <typename Bool, typename = std::enable_if_t<std::is_same_v<Bool, bool>>>
    void value (Bool v)
    {
        separate();
        text += v ? "true" : "false";
    }

    void value (int v)
    {
        separate();
        text += std::to_string (v);
    }

    /** A number, formatted by integer arithmetic and by nothing else.

        Never snprintf and never std::to_string(double). Both go through the C
        locale, so a machine whose LC_NUMERIC uses a comma writes 0,06 - which
        is not JSON - and both leave the rounding of a tie to the library. This
        output is compared byte for byte against a copy committed from another
        machine, so "the two libraries probably agree" is not an argument
        anybody can check. Same reasoning as src/lang/Rng.h writing out
        splitmix64 rather than delegating.

        Three decimals, trailing zeros dropped. The finest token is icon::hair
        at 0.07 and the coarsest a stroke at 2.0, so three is one more than
        anything needs and far inside a float's exact range. JSON has no notion
        of significant figures and every reader parses both spellings to the
        same double, so the shorter one is canonical.
    */
    void value (float v)
    {
        separate();

        constexpr long long scale = 1000;

        auto scaled = std::llround ((double) v * (double) scale);

        if (scaled < 0)
        {
            text += '-';
            scaled = -scaled;
        }

        text += std::to_string (scaled / scale);

        auto fraction = std::to_string (scaled % scale);
        fraction.insert (0, 3 - fraction.size(), '0');

        while (! fraction.empty() && fraction.back() == '0')
            fraction.pop_back();

        if (! fraction.empty())
            text += "." + fraction;
    }

    /** The finished document, with the trailing newline a text file has. */
    std::string str() const
    {
        return text + "\n";
    }

private:
    void open (char bracket)
    {
        separate();
        text += bracket;
        ++depth;
        first.push_back (true);
    }

    void close (char bracket)
    {
        const auto empty = first.back();

        first.pop_back();
        --depth;

        if (! empty)
            newline();

        text += bracket;
    }

    /** The comma, the newline and the indent, decided from whether anything has
        been written at this depth. Doing it here rather than after each value
        is what removes the need to look ahead for the last member.
    */
    void separate()
    {
        if (pending)
        {
            pending = false;
            return;
        }

        if (! first.empty())
        {
            if (! first.back())
                text += ',';

            first.back() = false;
        }

        if (depth > 0)
            newline();
    }

    void newline()
    {
        text += '\n';
        text.append ((std::size_t) depth * 2, ' ');
    }

    /** Backslash first, or every escape this adds is escaped again by the next
        replacement - the order cmake/GenStrings.cmake's dew_i18n_escape already
        argues for.

        UTF-8 above 0x7F passes through, which JSON permits and which keeps the
        schema's own prose readable in a diff.
    */
    void escapeInto (std::string_view v)
    {
        for (const auto c : v)
        {
            switch (c)
            {
                case '\\': text += "\\\\"; break;
                case '"': text += "\\\""; break;
                case '\n': text += "\\n"; break;
                case '\r': text += "\\r"; break;
                case '\t': text += "\\t"; break;

                default:
                    if ((unsigned char) c < 0x20)
                    {
                        static const char* digits = "0123456789abcdef";

                        text += "\\u00";
                        text += digits[((unsigned char) c >> 4) & 0xf];
                        text += digits[(unsigned char) c & 0xf];
                    }
                    else
                    {
                        text += c;
                    }

                    break;
            }
        }
    }

    std::string text;
    std::vector<bool> first;
    int depth = 0;

    /** True between a key and its value, so the value does not write a second
        comma and indent of its own. */
    bool pending = false;
};

} // namespace dew::docs
