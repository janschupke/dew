#include "model/ChannelColour.h"

#include "model/Ids.h"

namespace dew::channelColour
{

namespace
{

/** The ramp, once. Channels cycle through these so a new channel is
    immediately distinguishable from its neighbours without anyone choosing.
*/
const char* const ramp[] = {
    "ffe4572e", "ff29a19c", "ff4fa3ff", "fff2c14e",
    "ffb388eb", "ff3ecf8e", "ffff7eb6", "ff76c7c0",
};

constexpr int rampCount = (int) (sizeof (ramp) / sizeof (ramp[0]));

} // namespace

int rampSize() noexcept
{
    return rampCount;
}

juce::String defaultHex (int index)
{
    return ramp[((index % rampCount) + rampCount) % rampCount];
}

juce::Colour of (const juce::ValueTree& channel)
{
    // Stored without an alpha in every project dew has ever written, so the
    // last six digits are the colour and the alpha is always opaque. Taking
    // the LAST six rather than the first is what makes an "aarrggbb" value
    // read correctly too.
    const auto stored = channel[ids::colour].toString().getLastCharacters (6);

    // Length is not validity: "not a colour" also ends in six characters, and
    // juce reads every non-hex one as a zero, so it would come back as a very
    // dark almost-black rather than as an obvious mistake.
    if (stored.length() < 6 || ! stored.containsOnly ("0123456789abcdefABCDEF"))
        return juce::Colour::fromString (defaultHex (0));

    return juce::Colour::fromString ("ff" + stored);
}

} // namespace dew::channelColour
