#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** One value a message interpolates.

    Three types rather than juce::var, because a plural rule needs to know an
    integer IS an integer: var would answer isInt() for a double that happens to
    be whole, and "1.0 files" is the kind of defect nobody sees until a locale
    with a 'few' category arrives.
*/
class Arg
{
public:
    Arg() = default;

    Arg (juce::String value)
        : text (std::move (value))
        , kind (Kind::text)
    {
    }
    Arg (const char* value)
        : text (juce::CharPointer_UTF8 (value))
        , kind (Kind::text)
    {
    }
    Arg (int value)
        : number (value)
        , kind (Kind::integer)
    {
    }
    Arg (juce::int64 value)
        : number (value)
        , kind (Kind::integer)
    {
    }
    Arg (double value)
        : real (value)
        , kind (Kind::real)
    {
    }

    bool isNumber() const noexcept
    {
        return kind == Kind::integer || kind == Kind::real;
    }

    juce::int64 asInteger() const noexcept
    {
        return kind == Kind::real ? (juce::int64) real : number;
    }

    /** How this value reads where a message substitutes it, and what '#'
        becomes inside a plural branch. */
    juce::String toDisplayString() const;

private:
    enum class Kind
    {
        none,
        text,
        integer,
        real
    };

    juce::String text;
    juce::int64 number = 0;
    double real = 0.0;
    Kind kind = Kind::none;
};

/** The arguments one message is answered with.

    A fixed array rather than a map: eight is more than any message in dew takes
    and a lookup over eight names costs less than allocating a node. Formatting
    a string in a paint routine should not reach the heap for its arguments.

    The NAME is copied into the entry rather than pointed at, and that is not
    caution for its own sake. Every call site passes a literal, so a const char*
    would be correct in every one of them - and the first caller to pass
    anything else (a test, answering a message from the names the generator
    recorded) held a pointer into a temporary and got its placeholder back
    unsubstituted. A type that is only safe when its argument is a literal is a
    type that reads as safe and is not.
*/
class Args
{
public:
    static constexpr int capacity = 8;

    /** Longer than any argument name a message has reason to use. "channel" is
        the longest in the catalogue; sixteen leaves room without reaching the
        heap for a word. */
    static constexpr int maximumNameLength = 16;

    Args& with (const char* name, Arg value);

    /** The argument a plural message reads. Named, because 'count' is the one
        name the subset gives meaning to and spelling it at every call site is
        how one of them ends up spelling it 'n'. */
    Args& count (juce::int64 value)
    {
        return with ("count", value);
    }

    /** The value bound to `name`, or nullptr. */
    const Arg* find (juce::StringRef name) const noexcept;

private:
    struct Entry
    {
        char name[maximumNameLength] {};
        Arg value;
    };

    Entry entries[capacity];
    int size = 0;
};

} // namespace dew
