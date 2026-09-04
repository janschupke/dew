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
    /** What the glyph MEANS, which is what decides its colour.

        Roles rather than colours, the same way DewButton takes them: the
        transport bar knows that a button records and the design system knows
        what recording looks like, and a later theme change does not have to
        find every call site.

        Only four, and deliberately: an icon set where everything is coloured is
        an icon set where nothing is. `neutral` is the default and stays the
        overwhelming majority - tools, chevrons, zoom, stop. The other three are
        the glyphs a person looks for in a hurry.
    */
    enum class Role
    {
        neutral, ///< the text colour: says nothing beyond its shape
        go,      ///< starts sound: play
        record,  ///< arms or runs a take
        danger   ///< destroys something: every trash can
    };

    DewIconButton (juce::Path icon, const juce::String& tooltipText, Role = Role::neutral);

    void setRole (Role);

    void setIcon (juce::Path);

    /** What to offer when this control is right-clicked, or null for nothing.

        A CALLBACK rather than a target or a node, because dew_design "knows
        nothing about a project" - its own CMakeLists says so - and a control
        that held an AutomationTarget would know about one. The editor that
        BUILT this control from a ParamSpec is the one that knows which node it
        was built for, so it is the one that closes over it.
    */
    std::function<void()> onContextMenu;

    /** Sets the tooltip AND the accessible name, which are the same sentence.

        An override rather than a convention, because the convention had already
        failed: three buttons were constructed with an empty label and given
        their tooltip afterwards, so the status bar explained them and a screen
        reader found nothing to say. juce::Button seeds its name from the
        constructor argument once and setTooltip never touched it.
    */
    void setTooltip (const juce::String&) override;

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
    /** The glyph's colour at rest. Only at rest: a toggled button still crosses
        to onColour and a disabled one is still drained, because those say
        something about the button's STATE, which outranks what it means. */
    juce::Colour restingTint() const;

    ButtonLift lift { *this };

    juce::Path icon;
    juce::Colour onColour = tokens::colour::accent;
    Role role = Role::neutral;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewIconButton)
};

// -----------------------------------------------------------------------------

/** A compact letter toggle - the M and S on a mixer strip or channel row. */
class DewLetterToggle : public juce::Button
{
public:
    DewLetterToggle (const juce::String& letter, juce::Colour onColour,
                     const juce::String& tooltipText);

    /** Sets the tooltip AND the accessible name. A letter toggle needs this
        more than anything else in the set: its button text is "M", and "M" is
        not what a screen reader should read out for Mute.
    */
    void setTooltip (const juce::String&) override;

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

/** juce::ToggleButton, minus the defect that a right-click completes the toggle.

    A subclass rather than a repaint: DewLookAndFeel already maps the tick, its
    text and its disabled state onto tokens, so the stock control LOOKS like
    dew. What it does not do is refuse a right-click, and juce::Button completes
    a click for whichever mouse button pressed it. This is the same rule the
    hand-painted primitives above follow, applied to the one JUCE control dew
    still uses directly.
*/
class DewCheckbox : public juce::ToggleButton
{
public:
    explicit DewCheckbox (const juce::String& text = {});

    void mouseDown (const juce::MouseEvent&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewCheckbox)
};

// -----------------------------------------------------------------------------

/** juce::ComboBox, told what it looks like under the pointer.

    The same shape as DewCheckbox and for the same reason: DewLookAndFeel
    already paints the box, the arrow and the menu, so what the stock control
    lacks is not an appearance but a cursor - and JUCE does not inherit one from
    a parent, so a dropdown left alone shows an arrow while the button beside it
    shows a hand.

    Sixteen boxes in seven panels, which is exactly the count at which a habit
    stops being reliable, so a gate refuses a bare juce::ComboBox.
*/
/** A juce::Label whose colour is a ROLE rather than a value.

    juce::Label keeps a colour it was given, and a colour it was given is the
    palette that was in force when it was given - so a themed application comes
    back half painted, in whichever places nobody thought of. This holds the
    token instead and takes its value again whenever the look and feel changes.

    A POINTER into the palette is safe here in a way it would not have been
    before: tokens::colour names are references into the palette in force, so
    their addresses are fixed and their values are whatever the theme says.

    Only for a label whose colour is NOT the look and feel's own default. A
    label that wants textPrimary should say nothing and inherit it.
*/
class DewLabel : public juce::Label
{
public:
    DewLabel() = default;

    /** @param token  a tokens::colour name. Must outlive this, which every
                      token does - they are namespace-scope. */
    void setTextColourToken (const juce::Colour& token)
    {
        colourToken = &token;
        lookAndFeelChanged();
    }

    void lookAndFeelChanged() override
    {
        if (colourToken != nullptr)
            setColour (juce::Label::textColourId, *colourToken);
    }

private:
    const juce::Colour* colourToken = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewLabel)
};

// -----------------------------------------------------------------------------

class DewDropdown : public juce::ComboBox
{
public:
    explicit DewDropdown (const juce::String& name = {});

    /** A ComboBox puts its text colour on the juce::Label inside it, and does
        so from positionComboBoxText - on layout, not on paint. A box whose
        bounds do not change keeps the old palette's text through a theme
        change, so it is laid out again when the look and feel moves.

        The BASE call first, and it is not optional: ComboBox::lookAndFeelChanged
        replaces the label outright, and an override that skipped it would leave
        the old one - with the old palette on it - in place.
    */
    void lookAndFeelChanged() override
    {
        juce::ComboBox::lookAndFeelChanged();
        resized();
    }

    /** Sets the tooltip AND the accessible name, the way the hand-painted
        primitives do. juce::ComboBox reads its name from Component::getTitle,
        which nothing was setting, so eleven of dew's sixteen dropdowns had a
        status-bar explanation and nothing for a screen reader.
    */
    void setTooltip (const juce::String&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewDropdown)
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

    /** What this control DOES, as the colour of its value arc.

        Set for you by the ParamSpec constructor, which is why almost nothing
        calls this: every knob in the application is built from the catalog, so
        every knob is already the right colour. It is here for the two controls
        that are not - a free-form knob, and a stock juce::Slider that has to be
        told through Slider::trackColourId instead.
    */
    void setFunctionColour (juce::Colour);

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

    /** IGNORED, not inaccessible.

        A DewKnob is a juce::Component wrapping the juce::Slider that is the
        actual control, and the slider is what carries the role, the range and
        the value a screen reader reads out. Left alone the wrapper announces
        itself as an unnamed group containing one slider.

        setAccessible (false) would be the wrong tool: Component::isAccessible
        walks UP to its parent, so switching the wrapper off takes the slider
        inside it off too. An ignored handler is the one that means "skip me,
        keep my children".
    */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

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

    /** Accent until a ParamSpec says otherwise, so a knob built without one
        looks exactly as every knob used to. */
    juce::Colour functionColour { tokens::colour::accent };
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
void styleCaption (DewLabel&, const juce::String& text);

namespace paint
{
/** The rotary every knob in dew uses.
    @param proportion  0..1 position within the range
    @param bipolar     true for pan-like controls, where the arc fills out
                       from the centre instead of from the left
    @param value       the arc's colour: what this control DOES, from
                       palette::forRole. The track and the pointer are chrome
                       and stay as they are - a knob whose ring, needle and arc
                       were all one hue would be a coloured knob rather than a
                       knob that says something.
*/
void rotary (juce::Graphics&, juce::Rectangle<float>, float proportion, bool enabled, bool bipolar,
             juce::Colour value);

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

/** The ring that says a control has the keyboard.

    Drawn by the primitive itself rather than through
    LookAndFeel::createFocusOutlineForComponent, which puts the ring in its own
    overlay window: that needs a ComponentPeer, and every UI test here paints
    into an Image with no peer - so JUCE's mechanism would be invisible to the
    suite and to dew_shot, which is to say untestable in the two places this
    codebase actually looks at its own pixels.

    @param focused  whether the control holds the keyboard. Passed IN rather
                    than read from the component, because grabKeyboardFocus does
                    nothing without a ComponentPeer and this harness has none -
                    a helper that asked for itself could never be shown to draw.
                    Callers pass hasKeyboardFocus (true), which is also what a
                    DewKnob needs: its focus lives on the slider inside it.
*/
void focusRing (juce::Graphics&, const juce::Component&, bool focused);

/** The ring that says where the KEYBOARD is on a canvas that paints its
    contents.

    The same statement as a focus ring and deliberately the same colour: a
    control gets one around its edge, a canvas gets one around the cell the
    arrow keys are on. Drawn on an arbitrary rectangle rather than on a
    component, because the thing it marks is not one.

    Takes `shown` for the reason focusRing takes `focused` - a headless harness
    has no ComponentPeer, so a painter that asked the component whether it had
    the keyboard could never be shown to draw.
*/
void cursorOutline (juce::Graphics&, juce::Rectangle<float>, bool shown);

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
