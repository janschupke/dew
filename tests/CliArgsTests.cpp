#include <catch2/catch_test_macros.hpp>

#include "CliArgs.h"

using namespace dew;

namespace
{

CliArgs parse (const juce::StringArray& args) { return CliArgs { args }; }

} // namespace

TEST_CASE ("a long option takes the next argument", "[cli]")
{
    // juce::ArgumentList supports only the `=` form for long options, and
    // returns "no value" for this one. A render silently coming out four
    // seconds shorter than asked is why the parsing is dew's own.
    const auto args = parse ({ "song.dew", "out.wav", "--seconds", "4" });

    CHECK (args.has ("--seconds"));
    CHECK (args.value ("--seconds") == "4");
    CHECK (args.positional.size() == 2);
    CHECK (args.positional[0] == "song.dew");
}

TEST_CASE ("a long option takes an equals value", "[cli]")
{
    const auto args = parse ({ "--seconds=4", "--format=wav" });

    CHECK (args.value ("--seconds") == "4");
    CHECK (args.value ("--format") == "wav");
    CHECK (args.positional.isEmpty());
}

TEST_CASE ("a negative number is a value, not the next option", "[cli]")
{
    // dew_shot's copy of this parser was made before this rule existed, so
    // `--peak -1` set --peak to nothing and dropped the -1 entirely.
    const auto args = parse ({ "--peak", "-1" });

    CHECK (args.value ("--peak") == "-1");
    CHECK (args.positional.isEmpty());
    CHECK_FALSE (args.has ("-1"));
}

TEST_CASE ("a negative fraction is a value too", "[cli]")
{
    CHECK (parse ({ "--peak", "-.5" }).value ("--peak") == "-.5");
    CHECK (parse ({ "--peak", "-0.5" }).value ("--peak") == "-0.5");
}

TEST_CASE ("a flag with no value is present but empty", "[cli]")
{
    const auto args = parse ({ "--stems", "--verbose" });

    CHECK (args.has ("--stems"));
    CHECK (args.value ("--stems").isEmpty());
    CHECK (args.value ("--stems", "fallback") == "fallback");
}

TEST_CASE ("a trailing option with no value does not read past the end", "[cli]")
{
    const auto args = parse ({ "song.dew", "--seconds" });

    CHECK (args.has ("--seconds"));
    CHECK (args.value ("--seconds").isEmpty());
    CHECK (args.positional.size() == 1);
}

TEST_CASE ("an absent option falls back", "[cli]")
{
    const auto args = parse ({ "song.dew" });

    CHECK_FALSE (args.has ("--rate"));
    CHECK (args.value ("--rate", "44100") == "44100");
}

TEST_CASE ("a bare dash is treated as an option, not a file", "[cli]")
{
    // Not a negative number, so it takes the option path. Pinned because the
    // rule is a character test and "-" is its edge.
    const auto args = parse ({ "-" });

    CHECK (args.has ("-"));
    CHECK (args.positional.isEmpty());
}
