#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Animator.h"
#include "ui/design/Tokens.h"

namespace dew
{

/** A view dimension that a BUTTON moves smoothly and a DRAG moves at once.

    Zoom and lane height are reached four ways in dew, and they do not all want
    the same thing. The design rules are explicit about two of them: "a drag is
    never eased" and "the wheel is never eased at all" - both are continuous
    gestures, and easing a value the hand is already moving puts the view
    behind the pointer. A BUTTON and a keystroke are the opposite: one discrete
    jump of 1.5x, with nothing on screen explaining where the view went.

    So every zoom in the application cut, while the instrument panel folded
    beside them - and it is that mismatch, rather than the cut on its own, that
    reads as a bug.

    This eases the discrete path and stays out of the way of the other three.
    It READS the live value at the start of every animation rather than
    tracking it, which is the whole design: the alternative is a resync call at
    the end of every path that writes the value, and the first draft of this
    had eight of them and still missed the playlist's initial auto-fit - so the
    first zoom press after startup jerked the view to a stale 24 px/step and
    eased back down from there.

    Motion is off unless the application turns it on (see Animator), so under
    test and in dew_shot `animateTo` is an immediate write and every existing
    assertion about what a zoom button does still holds the moment it returns.
*/
class EasedZoom
{
public:
    /** @param readValue   what the view holds right now
        @param applyValue  writes it back - the same function the immediate
                           path already calls. Invoked once per frame. */
    EasedZoom (juce::Component& owner, std::function<double()> readValue,
               std::function<void (double)> applyValue)
        : read (std::move (readValue))
        , apply (std::move (applyValue))
        , motion (owner, 0.0f)
    {
        // Set at all because `apply` drives a LAYOUT rather than a repaint:
        // ComponentMotion's default is to repaint the owner, and repainting a
        // component does not lay it out again.
        motion.onChanged = [this]
        {
            // ComponentMotion notifies on snapTo as well as on a frame, and the
            // snap below is a read rather than a write - so the notification it
            // causes has to be swallowed, or every animation would begin by
            // writing the view's own value back into it.
            if (apply == nullptr || applying)
                return;

            const juce::ScopedValueSetter<bool> driving (applying, true);
            apply ((double) motion.get());
        };
    }

    /** The discrete path: a zoom button, a zoom key.

        Starts from wherever the view actually is, which is what makes the
        continuous paths free to write the value without telling this.
    */
    void animateTo (double target)
    {
        if (read != nullptr)
        {
            const juce::ScopedValueSetter<bool> quiet (applying, true);
            motion.snapTo ((float) read());
        }

        motion.animateTo ((float) target, tokens::motion::quickMs);
    }

private:
    std::function<double()> read;
    std::function<void (double)> apply;
    ComponentMotion motion;
    bool applying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasedZoom)
};

} // namespace dew
