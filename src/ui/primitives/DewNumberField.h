#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** A numeric field you change by dragging it.

    The interaction every DAW uses and JUCE has no equivalent of: press and drag
    up to increase, down to decrease; hold shift for fine steps; scroll to nudge;
    double-click to type an exact value. It replaces IncDecButtons sliders, which
    make the user hunt for a 12-pixel arrow to change a tempo.

    Drag distance maps to value through the field's own range, so a range of 20
    to 300 bpm and a range of -1 to 1 both feel the same to move.
*/
class DewNumberField : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    DewNumberField();
    ~DewNumberField() override;

    void setRange (double minimum, double maximum, double interval);
    void setValue (double newValue, juce::NotificationType = juce::sendNotification);
    double getValue() const noexcept { return value; }

    /** Text shown after the number, e.g. " bpm". */
    void setSuffix (juce::String);

    /** Decimal places shown. Set to 0 for an integer field. */
    void setNumDecimalPlaces (int places);

    /** Small caption drawn above the value. */
    void setCaption (juce::String);

    std::function<void()> onValueChange;

    /** Called once when a drag or typed edit begins, so callers can open a
        single undo transaction per gesture instead of one per pixel.
    */
    std::function<void()> onEditStart;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String displayText() const;
    void beginTypedEdit();
    void commit (double newValue);

    double value = 0.0;
    double minimum = 0.0, maximum = 1.0, interval = 0.01;
    int decimalPlaces = 2;
    juce::String suffix, caption;

    bool dragging = false;
    bool hovered = false;
    double valueAtDragStart = 0.0;

    std::unique_ptr<juce::TextEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewNumberField)
};

} // namespace dew
