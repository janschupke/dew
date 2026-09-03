#include "model/EntityColour.h"

#include "model/Ids.h"

namespace dew::entityColour
{

namespace
{

/** The ramp, once. Channels cycle through these so a new channel is
    immediately distinguishable from its neighbours without anyone choosing.
*/
const char* const ramp[] = {
    "ffe4572e", "ff29a19c", "ff4fa3ff", "fff2c14e", "ffb388eb", "ff3ecf8e", "ffff7eb6", "ff76c7c0",
};

constexpr int rampCount = (int) (sizeof (ramp) / sizeof (ramp[0]));

/** What each is called, in the same order. Plain colour words: a menu item is
    read at a glance, and nobody has to agree about what "Sage" is.
*/
const char* const rampNames[] = {
    "Red", "Teal", "Blue", "Amber", "Violet", "Green", "Pink", "Aqua",
};

static_assert ((int) (sizeof (rampNames) / sizeof (rampNames[0])) == rampCount,
               "every ramp entry has to have a name");

/** The six digits that are the colour, or empty when there are not six of them
    or one is not a hex digit.

    Length is not validity: "not a colour" also ends in six characters, and juce
    reads every non-hex one as a zero, so it would come back as a very dark
    almost-black rather than as an obvious mistake.
*/
juce::String sixHexDigits (const juce::ValueTree& node)
{
    // Stored without an alpha in every project dew has ever written, so the
    // last six digits are the colour and the alpha is always opaque. Taking the
    // LAST six rather than the first is what makes an "aarrggbb" value read
    // correctly too.
    const auto digits = node[ids::colour].toString().getLastCharacters (6);

    return (digits.length() == 6 && digits.containsOnly ("0123456789abcdefABCDEF"))
               ? digits
               : juce::String();
}

} // namespace

int rampSize() noexcept
{
    return rampCount;
}

juce::String defaultHex (int index)
{
    return ramp[((index % rampCount) + rampCount) % rampCount];
}

juce::String rampName (int index)
{
    return rampNames[((index % rampCount) + rampCount) % rampCount];
}

juce::Colour of (const juce::ValueTree& node)
{
    if (const auto chosen = stored (node))
        return *chosen;

    return juce::Colour::fromString (defaultHex (0));
}

std::optional<juce::Colour> stored (const juce::ValueTree& node)
{
    const auto digits = sixHexDigits (node);

    if (digits.isEmpty())
        return std::nullopt;

    return juce::Colour::fromString ("ff" + digits);
}

juce::String hexOf (juce::Colour colour)
{
    // Eight digits, alpha first, which is what every project dew has written
    // stores and what defaultHex returns.
    return colour.withAlpha (1.0f).toString();
}

} // namespace dew::entityColour
