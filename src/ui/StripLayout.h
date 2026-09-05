#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

namespace dew
{

/** Laying controls along a horizontal strip, left to right.

    The transport bar and the two editor toolbars each declared the same pair
    of lambdas - one to place a control and step past it, one to record where a
    group rule goes - and having written them separately they had already
    drifted: the toolbars stepped by space::xxs and the transport by space::xs,
    so the same three icon buttons were spaced differently in the strip above
    and the strip below.

    Controls are given a definite height rather than the strip's, because a
    26px control centred in a 46px transport bar is what makes the transport
    read as one row and not as a column of one.
*/
class StripLayout
{
public:
    /** @param strip     the whole strip
        @param insetX    horizontal margin
        @param insetY    vertical margin; what is left is the control height,
                         capped at size::controlHeight
    */
    StripLayout (juce::Rectangle<int> strip, int insetX, int insetY)
        : area (strip.reduced (insetX, insetY))
        , controlHeight (juce::jmin (tokens::size::controlHeight, area.getHeight()))
        , controlBand (centred (area))
    {
    }

    /** Places a control and steps past it, shrinking it toward `minimum` first.

        Returns false when it did not fit at all, in which case the control is
        HIDDEN and the caller is expected to offer it somewhere else - see
        ToolbarOverflow. Hiding it and saying nothing is what this used to do,
        and at 1.75x UI scale on a laptop it was silently dropping four groups
        off the piano roll's toolbar: the scale multiplies the PEER, so the
        LOGICAL window is what the display leaves rather than what was asked
        for, and the toolbar wants about 870 of it.

        removeFromLeft on an exhausted rectangle returns an empty one and then
        keeps returning empty ones, so without the check a narrow strip paints
        its overflow as a column of zero-width slivers at the right edge.

        `minimum` defaults to `width`, which is right for anything whose size is
        its shape - an icon button, a group of three.
    */
    bool place (juce::Component& c, int width, int minimum = -1)
    {
        const auto floor = minimum >= 0 ? juce::jmin (minimum, width) : width;

        if (area.getWidth() < floor)
        {
            c.setVisible (false);
            area.removeFromLeft (area.getWidth());
            return false;
        }

        c.setVisible (true);
        c.setBounds (centred (area.removeFromLeft (juce::jmin (width, area.getWidth()))));
        area.removeFromLeft (tokens::space::xxs);
        return true;
    }

    /** Takes a slot off the RIGHT end and gives it to a control. For the one
        button a strip needs only when it has run out of room. */
    void placeAtEnd (juce::Component& c, int width)
    {
        c.setVisible (true);
        c.setBounds (centred (area.removeFromRight (width)));
        area.removeFromRight (tokens::space::xxs);
    }

    /** A wider break, for controls that belong together but are not a group. */
    void gap()
    {
        area.removeFromLeft (tokens::space::sm);
    }

    /** A group break. Returns the x a rule should be drawn on, which the strip
        paints itself - the layout says where, the painter says how. */
    int divider()
    {
        area.removeFromLeft (tokens::space::sm);
        const auto x = area.getX();
        area.removeFromLeft (tokens::space::sm + tokens::space::xs);
        return x;
    }

    /** Places a control against the RIGHT end. The transport bar's scope is
        the only thing in a strip that should give way when the window does, so
        it is laid out from the end everything else is not. */
    juce::Rectangle<int> placeFromRight (int width)
    {
        return centred (area.removeFromRight (width));
    }

    /** A group break taken from the right, returning the rule's x. */
    int dividerFromRight()
    {
        area.removeFromRight (tokens::space::sm);
        const auto x = area.getRight();
        area.removeFromRight (tokens::space::sm + tokens::space::xs);
        return x;
    }

    /** What is left, for the one control that takes the rest of the strip. */
    juce::Rectangle<int> remaining() const noexcept
    {
        return centred (area);
    }

    /** The band the controls occupy: the strip's inset area, centred on a
        control's height. The transport bar draws its group rules down this, so
        a rule spans exactly what it separates rather than an inset somebody
        chose by eye - it was eight pixels, in a bar whose own inset is six.
    */
    juce::Rectangle<int> band() const noexcept
    {
        return controlBand;
    }

    int getRemainingWidth() const noexcept
    {
        return area.getWidth();
    }

    int getControlHeight() const noexcept
    {
        return controlHeight;
    }

private:
    /** A control's rectangle inside the slot it was given: as wide as the slot,
        as tall as a control, and CENTRED in it.

        withHeight, which this replaced, keeps the top edge - so a 26px control
        in a 46px transport bar inset by six sat with six pixels above it and
        fourteen below. The two editor toolbars are unmoved: a 34px strip inset
        by four leaves exactly a control's height, and centring something in a
        space its own size is where it already was.
    */
    juce::Rectangle<int> centred (juce::Rectangle<int> slot) const noexcept
    {
        return slot.withSizeKeepingCentre (slot.getWidth(), controlHeight);
    }

    juce::Rectangle<int> area;
    int controlHeight;
    juce::Rectangle<int> controlBand;
};

} // namespace dew
