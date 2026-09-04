#include "i18n/Arg.h"

namespace dew
{

juce::String Arg::toDisplayString() const
{
    switch (kind)
    {
        case Kind::text: return text;

        case Kind::integer: return juce::String (number);

        case Kind::real:
            // Trailing zeros trimmed, so a whole number written as a double
            // reads as one. A message says "2 bars", never "2.000000 bars".
            return juce::String (real, 6).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

        case Kind::none: break;
    }

    return {};
}

Args& Args::with (const char* name, Arg value)
{
    // Silently dropping the ninth argument would be a brace on screen with no
    // way to find out why. Nothing in dew comes close to eight, so this is a
    // programming error rather than a limit anybody will meet.
    jassert (size < capacity);

    if (size >= capacity)
        return *this;

    auto& entry = entries[size++];

    // Copied, not pointed at - see the note in Args. Truncation would make two
    // long names compare equal, so it is an assertion rather than a silent cut.
    auto length = 0;

    while (name != nullptr && name[length] != '\0' && length < maximumNameLength - 1)
    {
        entry.name[length] = name[length];
        ++length;
    }

    jassert (name == nullptr || name[length] == '\0');

    entry.name[length] = '\0';
    entry.value = std::move (value);

    return *this;
}

const Arg* Args::find (juce::StringRef name) const noexcept
{
    for (auto i = 0; i < size; ++i)
        if (name == juce::StringRef (entries[i].name))
            return &entries[i].value;

    return nullptr;
}

} // namespace dew
