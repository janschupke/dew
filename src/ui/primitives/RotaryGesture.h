#pragma once

#include <functional>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/primitives/DewControls.h"

namespace dew
{

/** The rule that makes dragging a rotary ONE undo step, said once.

    Nine panels wrote this out - the oscillator section, the sample and
    soundfont sections, the rack row, the playlist track header, the mixer
    strip, the effect card, the FM matrix and the instrument panel - and each
    one declared `inDrag` and `gestureActive` beside it. Two of those copies
    were byte-identical; the rest had drifted in where they put the guard and
    what they did after the write. It is the most-copied logic in the layer, and
    it is not layout: it is the mechanism that decides whether dragging a knob
    across its range is one undo step or four hundred, which SampleSection's own
    comment records getting wrong once already.

    What varies between the nine is the WRITE - a different tree, a different
    helper, a different early return - so that stays with the panel. What is the
    same is the protocol, which is all of this:

      - a press opens a gesture, and the first value in it opens the transaction
      - every value after that JOINS the transaction the press opened
      - a wheel notch or a keypress is not part of a drag and opens its own
      - a release ends the gesture, so the next value opens a new transaction

    Driven by the control's own drag callbacks rather than by the mouse: the
    pointer reads as "not down" in every headless harness, so a guard built on
    it would be one no test could ever see working.

    It knows nothing about a project, which is what lets it live here in
    dew_design beside the control it drives rather than up in dew_ui.
*/
class RotaryGesture
{
public:
    /** Wires a knob's three callbacks.

        `write` is handed `continuing` - whether this value joins the
        transaction the drag already opened - and returns whether it actually
        wrote. **A write that did not happen must not advance the gesture**: if
        it did, the next real value would be told to join a transaction that was
        never opened, and it would land in whatever transaction happened to be
        open instead. Three panels have an early return of their own for this
        reason, one of them past a tree that may not be valid yet.

        `onPress` is for the row that selects itself when you touch its knob.
    */
    void attach (DewKnob& knob, std::function<bool (bool continuing)> write,
                 std::function<void()> onPress = {})
    {
        knob.onEditStart = [this, onPress]
        {
            if (onPress != nullptr)
                onPress();

            begin();
        };

        knob.onEditEnd = [this] { end(); };

        knob.onValueChange = [this, doWrite = std::move (write)]
        {
            if (doWrite (gestureActive))
                advance();
        };
    }

    /** The same, for a control that is a juce::Slider rather than a DewKnob.

        A stepper writes what a knob writes, and the instrument panel already
        had one function under both - this is that function, minus the write.
    */
    void attach (juce::Slider& slider, std::function<bool (bool continuing)> write,
                 std::function<void()> onPress = {})
    {
        slider.onDragStart = [this, onPress]
        {
            if (onPress != nullptr)
                onPress();

            begin();
        };

        slider.onDragEnd = [this] { end(); };

        slider.onValueChange = [this, doWrite = std::move (write)]
        {
            if (doWrite (gestureActive))
                advance();
        };
    }

    /** True between a press and a release. */
    bool isDragging() const noexcept
    {
        return inDrag;
    }

private:
    void begin() noexcept
    {
        inDrag = true;
        gestureActive = false;
    }

    void end() noexcept
    {
        inDrag = false;
        gestureActive = false;
    }

    void advance() noexcept
    {
        gestureActive = inDrag;
    }

    bool inDrag = false;

    /** Whether a transaction is already open for this gesture. False until the
        first value of a drag has been written, which is what makes that first
        value open one and every value after it join. */
    bool gestureActive = false;
};

} // namespace dew
