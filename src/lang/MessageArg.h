#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace dew::lang
{

/** One value a diagnostic interpolates.

    The twin of i18n/Arg.h, over std::string rather than juce::String. Three
    types rather than one string, because a plural rule needs to know an integer
    IS an integer: "1.0 bars" is the kind of defect nobody sees until a locale
    with a `few` category arrives.
*/
class MsgArg
{
public:
    MsgArg() = default;

    MsgArg (std::string value)
        : text (std::move (value))
        , kind (Kind::text)
    {
    }

    MsgArg (std::string_view value)
        : text (value)
        , kind (Kind::text)
    {
    }

    MsgArg (const char* value)
        : text (value)
        , kind (Kind::text)
    {
    }

    /** Any integer, as ONE candidate.

        Three overloads - int, int64_t, size_t - is what this was, and it
        compiled on macOS for months. Under libstdc++ int64_t is `long` and
        size_t is `unsigned long`, so a `long long` matched none of them exactly
        and all of them by conversion: ambiguous, and only on the platform
        `scripts/linux-check.sh` builds. One constrained template has one
        candidate for every integer type there is.

        bool is excluded so that a flag cannot silently become the number 1
        where a message expected a word.
    */
    template <typename T,
              typename = std::enable_if_t<std::is_integral_v<T> && ! std::is_same_v<T, bool>>>
    MsgArg (T value)
        : number ((std::int64_t) value)
        , kind (Kind::integer)
    {
    }

    std::int64_t asInteger() const noexcept
    {
        return number;
    }

    /** How this value reads where a message substitutes it, and what '#'
        becomes inside a plural branch. */
    std::string toDisplayString() const;

private:
    enum class Kind
    {
        none,
        text,
        integer
    };

    std::string text;
    std::int64_t number = 0;
    Kind kind = Kind::none;
};

/** The arguments one message is answered with.

    A fixed array rather than a map, as i18n/Arg.h's Args is: six is more than
    any diagnostic in dew takes, and a lookup over six names costs less than
    allocating a node.

    The name is COPIED into the entry rather than pointed at. Every call site
    passes a literal, so a pointer would be correct in every one of them - and
    the first caller to pass anything else held a pointer into a temporary and
    got its placeholder back unsubstituted. That happened in dew_i18n, to this
    type's twin, within minutes of the gate that found it existing.
*/
class MsgArgs
{
public:
    static constexpr int capacity = 6;
    static constexpr int maximumNameLength = 16;

    MsgArgs& with (const char* name, MsgArg value);

    /** The argument a plural message reads. Named, because `count` is the one
        name the subset gives meaning to. */
    MsgArgs& count (std::int64_t value)
    {
        return with ("count", value);
    }

    /** The value bound to `name`, or nullptr. */
    const MsgArg* find (std::string_view name) const noexcept;

private:
    struct Entry
    {
        char name[maximumNameLength] {};
        MsgArg value;
    };

    Entry entries[capacity];
    int size = 0;
};

} // namespace dew::lang
