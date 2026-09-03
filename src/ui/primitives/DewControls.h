#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/WaveformPeaks.h"
#include "model/ParamSpec.h"
#include "ui/design/Animator.h"

#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"

namespace dew
{

/** How much lighter a button's fill gets under the pointer, eased.

    Every button in dew brightened by a step function - at rest, hovered,
    pressed - which is the one place a hard cut is most visible, because the
    pointer is right there. The three states are the same three; only the way
    they are reached changes.

    Driven from Button::buttonStateChanged rather than from paintButton: a
    paint that sets an animation target asks for a repaint from inside a
    repaint, and settles only because animateTo happens to be idempotent.
*/
class ButtonLift
{
public:
    explicit ButtonLift (juce::Button& b)
        : button (b)
        , motion (b)
        , toggled (b)
    {
    }

    /** Call from buttonStateChanged(), which JUCE sends for a change of toggle
        state as well as a change of mouse state. */
    void update();

    /** The fill, lifted by however far the animation has got. */
    juce::Colour apply (juce::Colour base) const;

    /** The same, crossing between an off colour and an on one.

        Mute, solo, arm and bypass all switched colour outright, and those are
        the four states a person flips most often while listening - the one
        moment a hard cut is most likely to be read as a glitch rather than as
        a change.
    */
    juce::Colour apply (juce::Colour off, juce::Colour on) const;

    /** The cross on its own, with no hover lift - for a border or a label,
        which the pointer does not brighten. Without this the fill crossed
        while the outline cut, which reads worse than either. */
    juce::Colour cross (juce::Colour off, juce::Colour on) const;

private:
    juce::Button& button;
    ComponentMotion motion;
    ComponentMotion toggled;
};

/** A text button in one of the system's roles.

    Roles rather than colours: callers say what a button MEANS and the design
    system decides how that looks, so a later theme change does not have to find
    every call site.
*/
class DewButton : public juce::Button
{
public:
    enum class Role
    {
        normal,
        primary,
        ghost,
        danger
    };

    explicit DewButton (const juce::String& text, Role = Role::normal);

    void setRole (Role);

    /** What to offer when this control is right-clicked, or null for nothing.

        A CALLBACK rather than a target or a node, because dew_design "knows
        nothing about a project" - its own CMakeLists says so - and a control
        that held an AutomationTarget would know about one. The editor that
        BUILT this control from a ParamSpec is the one that knows which node it
        was built for, so it is the one that closes over it.
    */
    std::function<void()> onContextMenu;

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

    /** Above Button's own handling, so a right-click opens the menu rather than
        arming a press that then never completes. */
    void mouseDown (const juce::MouseEvent&) override;

protected:
    void buttonStateChanged() override
    {
        lift.update();
    }

private:
    ButtonLift lift { *this };

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

    /** What to offer when this control is right-clicked, or null for nothing.

        A CALLBACK rather than a target or a node, because dew_design "knows
        nothing about a project" - its own CMakeLists says so - and a control
        that held an AutomationTarget would know about one. The editor that
        BUILT this control from a ParamSpec is the one that knows which node it
        was built for, so it is the one that closes over it.
    */
    std::function<void()> onContextMenu;

    /** Colour used when the button is toggled on. Defaults to the accent. */
    void setOnColour (juce::Colour);

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;
    void mouseDown (const juce::MouseEvent&) override;

protected:
    void buttonStateChanged() override
    {
        lift.update();
    }

private:
    ButtonLift lift { *this };

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

    /** What to offer when this control is right-clicked, or null for nothing.

        A CALLBACK rather than a target or a node, because dew_design "knows
        nothing about a project" - its own CMakeLists says so - and a control
        that held an AutomationTarget would know about one. The editor that
        BUILT this control from a ParamSpec is the one that knows which node it
        was built for, so it is the one that closes over it.
    */
    std::function<void()> onContextMenu;

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;
    void mouseDown (const juce::MouseEvent&) override;

protected:
    void buttonStateChanged() override
    {
        lift.update();
    }

private:
    ButtonLift lift { *this };

    juce::String letter;
    juce::Colour onColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewLetterToggle)
};

// -----------------------------------------------------------------------------

/** A rotary with its caption and value drawn as one unit, so knobs line up
    without every caller laying out a separate label.
*/
class DewKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    DewKnob (const juce::String& caption, double minimum, double maximum, double interval);

    /** A knob built from what the catalog declares the parameter to be: its
        caption, its range, its step, its decimals, whether it is bipolar and
        whether it sweeps logarithmically.

        Six knobs and a stepper stated those a second time by hand, and every
        one of them had drifted from the engine's own clamp.
    */
    explicit DewKnob (const ParamSpec&);

    void setValue (double, juce::NotificationType = juce::sendNotification);
    double getValue() const noexcept
    {
        return slider.getValue();
    }

    void setNumDecimalPlaces (int);

    /** Bipolar knobs fill out from the centre - pan, detune, EQ gain. */
    void setBipolar (bool);

    /** Drops the caption and the value readout and gives the rotary the whole
        component, for a knob that has to fit on a 34px row. The caption then has
        nowhere to be drawn, so a compact knob says what it is through its
        tooltip and through being bipolar or not.
    */
    void setCompact (bool);

    /** Sets it on BOTH this and the slider inside.

        The slider, because juce::TooltipWindow hit-tests the deepest component
        under the pointer and that is what the pointer is actually over. And on
        this, because the status bar's hover help walks UP from wherever the
        event landed - a knob whose tooltip lived only on its child was a knob
        that could not be found from outside, which is how six of them reached
        the window with nothing to say.
    */
    void setTooltip (const juce::String&) override;

    juce::Slider& getSlider() noexcept
    {
        return slider;
    }

    std::function<void()> onValueChange;

    /** A drag begins and ends. Both halves matter: a caller opens one undo
        transaction on the first and stops re-opening it on the second, which is
        what makes a whole gesture a single undo step.
    */
    std::function<void()> onEditStart;
    std::function<void()> onEditEnd;

    /** What to offer when this control is right-clicked, or null for nothing.

        A CALLBACK rather than a target or a node, because dew_design "knows
        nothing about a project" - its own CMakeLists says so - and a control
        that held an AutomationTarget would know about one. The editor that
        BUILT this control from a ParamSpec is the one that knows which node it
        was built for, so it is the one that closes over it.
    */
    std::function<void()> onContextMenu;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Shift at the moment of PRESS makes the whole drag a fine one.

        Fixed for the gesture rather than live, because JUCE computes a rotary's
        value from where the drag began and the current sensitivity: changing
        it mid-drag rescales the travel already made and the knob jumps.
    */
    void mouseDown (const juce::MouseEvent&) override;

private:
    /** Where the needle actually is, which is not always where the value is.

        Three rules, in priority order, and each of them is load-bearing:

          1. Animation is off unless the application turns it on. That is what
             keeps a headless render of a knob a render of the value it was
             given.
          2. A DRAG is never eased. Easing a control against the pointer that is
             dragging it feels broken, because the two disagree the whole way.
          3. The FIRST value a knob is ever given snaps. A panel built from a
             document must not sweep every knob up from zero.

        The numeric readout is not eased. A number is read and a needle is seen;
        a readout that arrived 120ms late would just look wrong.
    */
    void updateNeedle();
    float proportionOfValue() const;

    juce::String caption;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    ComponentMotion needle { *this };
    bool needleSeeded = false;
    bool dragging = false;
    int decimalPlaces = 3;
    bool bipolar = false;
    bool compact = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewKnob)
};

// -----------------------------------------------------------------------------

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

/** The word beside a control that is not a knob, as a Label.

    paint::caption is the same statement DRAWN, for a component that paints its
    own; this is for the case where the caption has to be laid out beside a
    juce::ComboBox rather than painted over it. The instrument panel had it as a
    file-local helper, and the two toolbars that wanted the same word next to
    the same kind of control would each have grown their own.
*/
void styleCaption (juce::Label&, const juce::String& text);

namespace paint
{
/** The rotary every knob in dew uses.
    @param proportion  0..1 position within the range
    @param bipolar     true for pan-like controls, where the arc fills out
                       from the centre instead of from the left
*/
void rotary (juce::Graphics&, juce::Rectangle<float>, float proportion, bool enabled, bool bipolar);

void surface (juce::Graphics&, juce::Rectangle<int>, juce::Colour);
void wellBackground (juce::Graphics&, juce::Rectangle<int>);

/** The card a group of controls sits on: a rounded surface with a hairline
    edge.

    The mixer strip and the effect card each hand-rolled this same fill and
    border, and the panels that hold them drew nothing at all - so an effect
    chain floated on the window background with no edge to say where it
    began. There was a DewPanel class meant for this; nothing ever
    instantiated it, so it had drifted into being a fourth opinion rather
    than the shared one.
*/
void container (juce::Graphics&, juce::Rectangle<int>);

/** Fills the region beyond the content with a visibly inert texture, so an
    empty area reads as "nothing here" rather than as a broken control.

    For a region with nothing to continue into it - the panel below the last
    channel row. Where the grid DOES continue, use beyondEnd() instead.
*/
void inertArea (juce::Graphics&, juce::Rectangle<int>);

/** Marks the part of a timeline that is past the end of the material.

    Drawn OVER a grid that has already been painted across the full width,
    so the rows and bar lines keep going and the region still reads as
    out of bounds. Replacing the grid with a hatch, which is what this used
    to do, left a dead rectangle wherever the view was wider than the music.
*/
void beyondEnd (juce::Graphics&, juce::Rectangle<int>, float edgeX);

/** A control's caption - the word under a knob or beside a number field.
    The smallest thing in the system, and deliberately so.
*/
void caption (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
              juce::Justification = juce::Justification::centredLeft);

/** A panel's heading. Distinct from caption(): "EFFECTS" is a heading and
    "CUTOFF" is a caption, and drawing both at the same size was why the
    effect chain's own title read as smaller than the things inside it.
*/
void sectionHeading (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                     juce::Justification = juce::Justification::centredLeft);

/** Every "there is nothing here yet" message.

    One function rather than five, because when each panel picked its own
    size and colour the app ended up with the same kind of message drawn at
    10, 11 and 13 point, in two different greys - and the 11pt textDisabled
    one was unreadable against the hatch behind it.
*/
void emptyState (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                 juce::Justification = juce::Justification::centred);

/** A component's own rectangle, inset half a pixel.

    Written out ten times, because a one-pixel edge drawn on a whole
    coordinate straddles two pixels and comes out two pixels wide and grey.
    The half is stroke::whisper - half of the hairline it is making room
    for - which is why that token exists.
*/
juce::Rectangle<float> bodyRect (const juce::Component&);

/** A sample's waveform: one column of pixels per column of pixels, each
    showing the extremes over the span it covers.

    Picking a single bin per column instead makes a waveform shimmer as the
    view resizes, which is why all three painters did it this way - and
    having written it three times they had drifted to insets of 2, 2 and 3,
    so the same audio was a pixel taller in the sequencer than in the
    playlist.

    @param y        the full vertical extent; the trace is inset within it
    @param span     where the whole file maps to horizontally, which may
                    reach outside the visible area
    @param painted  the columns actually to draw
    @param colourAt the colour for a column, so a trim handle can dim what
                    is outside it without a second loop
*/
void waveform (juce::Graphics&, juce::Range<float> y, juce::Range<float> span,
               juce::Range<float> painted, const WaveformPeaks&,
               const std::function<juce::Colour (float x)>& colourAt);
} // namespace paint

} // namespace dew
