#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

namespace dew::silence
{

/** How a channel, a lane or an insert that has been turned OFF is presented.

    It was three answers, and two of them were "nothing".

      - the channel rack dimmed the STEPS and left the row alone, so a muted
        channel's header looked exactly like a channel that was playing
      - the playlist drew a scrim over its header's ground and then painted the
        lane colour on top of it, and its name and its power glyph are CHILD
        components, which paint after their parent - so the dim reached the
        ground and nothing standing on it
      - the mixer did not dim anything at all: a muted insert was told apart
        from a live one by the colour of one 24px glyph

    One rule, in two halves, because a row is a component that paints a ground
    and parents the controls standing on it, and a scrim can only reach the
    first. `paintOver` dims what the row drew; `applyTo` dims what it holds.

    The control that turns the row back ON stays bright. That is a deliberate
    exception rather than an oversight: the way out of a state must not be
    drawn in that state.
*/

/** The scrim, over everything the row painted - the ground, its colour band and
    its rule.

    Painted LAST rather than under the colour band, which is where the
    playlist's was: a band at full strength on a dimmed ground is the brightest
    thing on a row that is meant to be receding.
*/
inline void paintOver (juce::Graphics& g, juce::Rectangle<int> bounds, bool silent)
{
    if (! silent)
        return;

    g.setColour (tokens::colour::wellDeep.withAlpha (tokens::emphasis::subdued));
    g.fillRect (bounds);
}

/** Dims every control ON the row, which a scrim cannot reach.

    Component::setAlpha covers a component and its children, so one call per
    control covers whatever each of them is made of. Set in BOTH directions, so
    turning a row back on is not a state that needs its own call site.

    @param keepBright  the control that turns the row back on, or null
*/
inline void applyTo (juce::Component& row, bool silent, const juce::Component* keepBright = nullptr)
{
    for (auto* child : row.getChildren())
        child->setAlpha (silent && child != keepBright ? tokens::emphasis::subdued : 1.0f);
}

} // namespace dew::silence
