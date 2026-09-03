#pragma once

#include <optional>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace dew
{

/** What a thing in the project is painted in: where the ramp lives, and the one
    way to read a colour off a node.

    It was channelColour, and only a channel had one. A playlist lane took the
    colour of its POSITION in the list and a mixer strip took the colours of the
    channels routed into it, so neither could be changed and moving a lane
    repainted it. All three carry one now, and all three read it through here.

    Three things wanted this and each had its own answer. The document's
    factory carried a four-entry table of hex strings; the design system
    carried an eight-entry ramp that nothing outside a test referred to; and
    five painters each wrote out

        juce::Colour::fromString ("ff" + channel[ids::colour].toString()
                                             .getLastCharacters (6))

    which is not obvious enough to be written five times. A channel whose
    colour property is missing or malformed used to come back as transparent
    black from three of them and as something else from the other two.

// clang-format off
    The ramp is the designed one - it is eight long, not four, and its first
    four entries are byte-identical to the factory's, so no example project
    changes. tokens::colour::channelRamp states the same eight values for
    painting things with no stored colour of their own, and a test holds the
    two together rather than a dependency, so the design library keeps knowing
    nothing about the document model.
*/
namespace entityColour
{
/** How many distinct colours a project cycles through. */
int rampSize() noexcept;

/** What the nth ramp entry is CALLED.

    A menu has to say something, and "Colour 4" says nothing about which one
    it is. Here rather than beside the ramp in the design system for the
    reason the ramp itself is duplicated: dew_design must keep knowing
    nothing about the document, and it is the document's menu that needs a
    word.
*/
juce::String rampName (int index);

/** The stored form - eight hex digits, "aarrggbb" - for the nth channel.
    Wraps, so any index is valid. */
juce::String defaultHex (int index);

/** What a node is painted in. Falls back to the first ramp entry when the
    property is absent or unreadable, because a channel with no colour is
    still a channel and must not paint as transparent black. */
juce::Colour of (const juce::ValueTree& node);

/** The colour this node CHOSE, or nothing.

    The difference from of() is the whole of what "inherit" means. A lane
    and a strip have a sensible colour without choosing one - a lane from
    its position, a strip from what is routed into it - and the empty
    property says to keep using it. of() cannot express that, because it has
    to return a colour; a caller that has something to fall back to asks
    this instead and falls back itself.
*/
std::optional<juce::Colour> stored (const juce::ValueTree& node);

/** The stored form of `colour`, or an empty string meaning "inherit". */
juce::String hexOf (juce::Colour);
} // namespace entityColour

// clang-format on
} // namespace dew
