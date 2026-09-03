#pragma once

#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Animator.h"

namespace dew
{

/** Whether the pointer is over a component, tracked once and correctly.

    Three surfaces did this by hand - the channel rack's row, the mixer strip
    and the effect card - with the same three-line setHovered and the same pair
    of overrides. Two of the three were right. The rack's said

        void mouseExit (const MouseEvent&) override { setHovered (! isMouseOver (true)); }

    so a rack row lit up when the pointer LEFT it and went dark when it arrived.

    The negation is not the interesting part; the re-ask is. mouseExit fires
    when the pointer moves onto a CHILD, which is still inside the component,
    so the exit handler has to ask the component whether the pointer is really
    gone - `isMouseOver (true)`, with the true meaning "counting children". A
    plain `setHovered (false)` there makes a row flicker off whenever the
    cursor crosses one of its own buttons. That reasoning now lives in one
    place, and the fourth surface to want hover gets it right by construction.
*/
class HoverTracker
{
public:
    explicit HoverTracker (juce::Component& c)
        : owner (c)
    {
    }

    /** Call from the owner's mouseEnter. */
    void enter()
    {
        set (true);
    }

    /** Call from the owner's mouseExit - the pointer may have landed on a child. */
    void exit()
    {
        set (owner.isMouseOver (true));
    }

    bool isHovered() const noexcept
    {
        return hovered;
    }

    /** Run when the state changes. Repaints if nothing is attached, which is
        what all three call sites did; a widget that animates its hover sets
        this instead. */
    std::function<void()> onChange;

    /** How far a hovered surface is lifted, eased.

        A surface, not a control: a mixer strip flaring as hard as a button
        would is why emphasis has two lift rungs. Reading it is free, so paint()
        pulls a number rather than branching on a bool.
    */
    float lift() const noexcept
    {
        return motion.get();
    }

private:
    void set (bool shouldBeHovered)
    {
        if (std::exchange (hovered, shouldBeHovered) == shouldBeHovered)
            return;

        motion.animateTo (hovered ? tokens::emphasis::surfaceLift : 0.0f, tokens::motion::quickMs);

        if (onChange != nullptr)
            onChange();
        else
            owner.repaint();
    }

    juce::Component& owner;
    bool hovered = false;
    ComponentMotion motion { owner };
};

} // namespace dew
