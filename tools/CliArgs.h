#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** True for something like "-1" or "-.5", as opposed to an option. */
inline bool looksLikeNegativeNumber (const juce::String& text)
{
    if (! text.startsWith ("-") || text.length() < 2)
        return false;

    const auto next = text[1];
    return juce::CharacterFunctions::isDigit (next) || next == '.';
}

/** A command line, parsed so that both `--opt value` and `--opt=value` work.

    juce::ArgumentList deliberately supports only the `=` form for long options
    (getValueForOption returns the value after `=`, and takes the NEXT argument
    only for short options). Accepting `--seconds 4` silently as "no value" is
    exactly the kind of thing nobody notices until a render comes out the wrong
    length, so the parsing is done here instead.

    A value that is a negative number is taken as a value rather than as the next
    option, so `--peak -1` means what it looks like.

    Shared by dew_render and dew_shot. It was written twice, and the second copy
    was made BEFORE the negative-number rule existed - so `dew_shot --peak -1`
    parsed `-1` as an option and dropped it, silently, for as long as both copies
    existed.
*/
struct CliArgs
{
    CliArgs (int argc, char* argv[])
    {
        juce::StringArray raw;

        for (int i = 1; i < argc; ++i)
            raw.add (juce::String::fromUTF8 (argv[i]));

        parse (raw);
    }

    /** For tests, which have arguments but no argv. */
    explicit CliArgs (const juce::StringArray& raw) { parse (raw); }

    bool has (const juce::String& name) const { return options.containsKey (name); }

    juce::String value (const juce::String& name, const juce::String& fallback = {}) const
    {
        return has (name) && options[name].isNotEmpty() ? options[name] : fallback;
    }

    juce::StringPairArray options;
    juce::StringArray positional;

private:
    void parse (const juce::StringArray& raw)
    {
        for (int i = 0; i < raw.size(); ++i)
        {
            const auto& arg = raw[i];

            if (! arg.startsWith ("-") || looksLikeNegativeNumber (arg))
            {
                positional.add (arg);
                continue;
            }

            const auto name = arg.upToFirstOccurrenceOf ("=", false, false);

            if (arg.contains ("="))
            {
                options.set (name, arg.fromFirstOccurrenceOf ("=", false, false));
            }
            else if (i + 1 < raw.size()
                     && (! raw[i + 1].startsWith ("-") || looksLikeNegativeNumber (raw[i + 1])))
            {
                options.set (name, raw[i + 1]);
                ++i;
            }
            else
            {
                options.set (name, "");
            }
        }
    }
};

} // namespace dew
