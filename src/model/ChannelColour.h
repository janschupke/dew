#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace dew
{

/** A channel's colour: where the ramp lives, and the one way to read one.

    Three things wanted this and each had its own answer. The document's
    factory carried a four-entry table of hex strings; the design system
    carried an eight-entry ramp that nothing outside a test referred to; and
    five painters each wrote out

        juce::Colour::fromString ("ff" + channel[ids::colour].toString()
                                             .getLastCharacters (6))

    which is not obvious enough to be written five times. A channel whose
    colour property is missing or malformed used to come back as transparent
    black from three of them and as something else from the other two.

    The ramp is the designed one - it is eight long, not four, and its first
    four entries are byte-identical to the factory's, so no example project
    changes. tokens::colour::channelRamp states the same eight values for
    painting things with no stored colour of their own, and a test holds the
    two together rather than a dependency, so the design library keeps knowing
    nothing about the document model.
*/
namespace channelColour
{
    /** How many distinct colours a project cycles through. */
    int rampSize() noexcept;

    /** The stored form - eight hex digits, "aarrggbb" - for the nth channel.
        Wraps, so any index is valid. */
    juce::String defaultHex (int index);

    /** What a channel is painted in. Falls back to the first ramp entry when
        the property is absent or unreadable, because a channel with no colour
        is still a channel and must not paint as transparent black. */
    juce::Colour of (const juce::ValueTree& channel);
}

} // namespace dew
