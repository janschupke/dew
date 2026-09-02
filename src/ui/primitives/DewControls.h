#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../design/Icons.h"
#include "../design/Tokens.h"

namespace dew
{

/** A text button in one of the system's roles.

    Roles rather than colours: callers say what a button MEANS and the design
    system decides how that looks, so a later theme change does not have to find
    every call site.
*/
class DewButton : public juce::Button
{
public:
    enum class Role { normal, primary, ghost, danger };

    explicit DewButton (const juce::String& text, Role = Role::normal);

    void setRole (Role);

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    Role role = Role::normal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewButton)
};

// -----------------------------------------------------------------------------

/** A square button showing one icon. Toggles when setClickingTogglesState is on,
    which is how mute, solo and effect bypass are drawn.
*/
class DewIconButton : public juce::Button
{
public:
    DewIconButton (juce::Path icon, const juce::String& tooltipText);

    void setIcon (juce::Path);

    /** Colour used when the button is toggled on. Defaults to the accent. */
    void setOnColour (juce::Colour);

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    juce::Path icon;
    juce::Colour onColour = tokens::colour::accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewIconButton)
};

// -----------------------------------------------------------------------------

/** A compact letter toggle - the M and S on a mixer strip or channel row. */
class DewLetterToggle : public juce::Button
{
public:
    DewLetterToggle (const juce::String& letter, juce::Colour onColour,
                     const juce::String& tooltipText);

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    juce::String letter;
    juce::Colour onColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewLetterToggle)
};

// -----------------------------------------------------------------------------

/** A rotary with its caption and value drawn as one unit, so knobs line up
    without every caller laying out a separate label.
*/
class DewKnob : public juce::Component
{
public:
    DewKnob (const juce::String& caption, double minimum, double maximum, double interval);

    void setValue (double, juce::NotificationType = juce::sendNotification);
    double getValue() const noexcept { return slider.getValue(); }

    void setNumDecimalPlaces (int);

    /** Bipolar knobs fill out from the centre - pan, detune, EQ gain. */
    void setBipolar (bool);

    juce::Slider& getSlider() noexcept { return slider; }

    std::function<void()> onValueChange;
    std::function<void()> onEditStart;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String caption;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    int decimalPlaces = 3;
    bool bipolar = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewKnob)
};

// -----------------------------------------------------------------------------

/** A titled panel. Draws the surface, the heading and the divider so panels do
    not each invent their own.
*/
class DewPanel : public juce::Component
{
public:
    explicit DewPanel (juce::String title = {});

    void setTitle (juce::String);

    /** Bounds inside the heading and the standard inset. */
    juce::Rectangle<int> getContentBounds() const;

    void paint (juce::Graphics&) override;

private:
    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewPanel)
};

// -----------------------------------------------------------------------------

/** Shared drawing the editors use, so a bar line looks the same everywhere. */
/** Makes mouse events that land on a child widget also reach `parent`.

    Used for hover: a row should light up while the pointer is over one of its
    buttons, and without this the row only ever sees the pointer leave.

    Deliberately NOT the mechanism for selection. JUCE delivers these through
    ComponentPeer, which cannot be driven in a headless test, so anything that
    matters is wired explicitly instead - a fader that selects its strip does it
    through its own onDragStart, which a test can verify exists and works.
*/
void forwardChildMouseEventsTo (juce::Component& parent);

namespace paint
{
    /** The rotary every knob in dew uses.
        @param proportion  0..1 position within the range
        @param bipolar     true for pan-like controls, where the arc fills out
                           from the centre instead of from the left
    */
    void rotary (juce::Graphics&, juce::Rectangle<float>, float proportion,
                 bool enabled, bool bipolar);

    void surface (juce::Graphics&, juce::Rectangle<int>, juce::Colour);
    void wellBackground (juce::Graphics&, juce::Rectangle<int>);

    /** Fills the region beyond the content with a visibly inert texture, so an
        empty area reads as "nothing here" rather than as a broken control.
    */
    void inertArea (juce::Graphics&, juce::Rectangle<int>);

    void caption (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                  juce::Justification = juce::Justification::centredLeft);
}

} // namespace dew
