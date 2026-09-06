#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"
#include "ui/primitives/ButtonBehaviour.h"

namespace dew
{

/* The four buttons, the label and the dropdown.

   One family: everything a person clicks that is not a knob. Icons.h is
   included HERE, where DewIconButton needs it, rather than by everything that
   wanted a checkbox.
*/

/** A text button in one of the system's roles.

    Roles rather than colours: callers say what a button MEANS and the design
    system decides how that looks, so a later theme change does not have to find
    every call site.
*/
class DewButton : public PaintedButton<juce::Button>
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

    /** Where the label sits. Centred by default, which is what a button is.

        A ghost button used as a LIST ROW is the case this exists for - a
        preferences category, a search result - where centred text reads as a
        row of floating labels rather than as a list. Anything but centred is
        inset by one gap, because text against the very edge of a row reads as
        clipped.
    */
    void setTextJustification (juce::Justification);

    /** A picture beside the word, or an empty path for none.

        A PATH rather than an icon's name, for the reason onContextMenu is a
        callback: dew_design "knows nothing about a project" - its own
        CMakeLists says so - and a button that named an instrument type would
        know about one. The editor that knows what the button adds is the one
        that chooses the shape.

        The glyph and the label are centred as one group, so a button with a
        picture is still a centred button rather than a left-aligned one that
        happens to have space on the right.
    */
    void setGlyph (juce::Path);

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    Role role = Role::normal;
    juce::Justification justification { juce::Justification::centred };
    juce::Path glyph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewButton)
};

// -----------------------------------------------------------------------------

/** A square button showing one icon. Toggles when setClickingTogglesState is on,
    which is how mute, solo and effect bypass are drawn.
*/
class DewIconButton : public PaintedButton<juce::Button>
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
        record,  ///< arms or runs a take: rests in red, FILLS with the deep red
        danger   ///< destroys something: every trash can
    };

    DewIconButton (juce::Path icon, const juce::String& tooltipText, Role = Role::neutral);

    void setRole (Role);

    void setIcon (juce::Path);

    /** Which glyph it is showing.

        A read-only accessor, and it exists for one reason: the transport bar's
        play button carries the transport's STATE in its glyph, and the only
        way to hold "the icon follows the engine, whatever moved it" is to be
        able to read the icon back.
    */
    const juce::Path& getIcon() const noexcept
    {
        return icon;
    }

    /** Sets the tooltip AND the accessible name, which are the same sentence.

        An override rather than a convention, because the convention had already
        failed: three buttons were constructed with an empty label and given
        their tooltip afterwards, so the status bar explained them and a screen
        reader found nothing to say. juce::Button seeds its name from the
        constructor argument once and setTooltip never touched it.
    */
    void setTooltip (const juce::String&) override;

    /** Colour used when the button is toggled on. Defaults to what the ROLE
        means - the accent for most, the deep record red for Role::record. Set
        it by hand only for a fill the role does not already name. */
    void setOnColour (juce::Colour);

    /** The fill a role crosses to when the button goes on.

        Part of the role rather than of the call site, because recording is a
        FILL-ONLY colour: it is deliberately too deep to read as a glyph, and
        leaving it to be assigned by hand is what would let it be handed to
        something that draws with it. See restingTint. */
    static juce::Colour onColourFor (Role) noexcept;

    /** A glyph in a square, not a word in a box - see tokens::size::iconButton. */
    int preferredHeight() const
    {
        return tokens::size::iconButton;
    }

    /** onClick, plus what was held down while it was clicked.

        juce::Button::onClick takes nothing, and the modifiers ARE the gesture
        for the one control that needs this: a track's on/off indicator, where
        shift means "say that of every track". Reading
        ModifierKeys::getCurrentModifiers instead would be a global read of the
        real keyboard, which is not a thing a headless harness has - and this
        codebase has been caught by that class of API twice already.

        Called before onClick, and both fire: a control that wants only the
        plain click carries on using onClick and never sees this.
    */
    std::function<void (const juce::ModifierKeys&)> onModifiedClick;

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

    /** Where the modifiers are on the way to onClick. juce::Button routes every
        completed click - a press, the space bar, triggerClick - through here. */
    void clicked (const juce::ModifierKeys& mods) override
    {
        if (onModifiedClick != nullptr)
            onModifiedClick (mods);

        juce::Button::clicked (mods);
    }

private:
    /** The glyph's colour at rest. Only at rest: a toggled button still crosses
        to onColour and a disabled one is still drained, because those say
        something about the button's STATE, which outranks what it means. */
    juce::Colour restingTint() const;

    juce::Path icon;
    juce::Colour onColour = tokens::colour::accent;
    Role role = Role::neutral;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewIconButton)
};

// -----------------------------------------------------------------------------

/** A compact letter toggle - a single character in a 24px square.

    It was the M and the S on a mixer strip and a channel row; both are gone,
    collapsed into one on/off indicator that says a state rather than naming a
    control. What is left is the channel rack's R, which is the case a letter
    still suits: arming is a mode with a name, not a state with a picture.
*/
class DewLetterToggle : public PaintedButton<juce::Button>
{
public:
    DewLetterToggle (const juce::String& letter, juce::Colour onColour,
                     const juce::String& tooltipText);

    /** Sets the tooltip AND the accessible name. A letter toggle needs this
        more than anything else in the set: its button text is "M", and "M" is
        not what a screen reader should read out for Mute.
    */
    void setTooltip (const juce::String&) override;

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

    /** A single character in its own square. */
    int preferredHeight() const
    {
        return tokens::size::letterToggle;
    }

private:
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
class DewCheckbox : public PopupSafeButton<juce::ToggleButton>
{
public:
    explicit DewCheckbox (const juce::String& text = {});

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewCheckbox)
};

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------

/** juce::ComboBox, told what it looks like under the pointer.

    The same shape as DewCheckbox and for the same reason: DewLookAndFeel
    already paints the box, the arrow and the menu, so what the stock control
    lacks is not an appearance but a cursor - and JUCE does not inherit one from
    a parent, so a dropdown left alone shows an arrow while the button beside it
    shows a hand.

    Sixteen boxes in seven panels, which is exactly the count at which a habit
    stops being reliable, so a gate refuses a bare juce::ComboBox.
*//** A juce::Label whose colour is a ROLE rather than a value.

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

    /** How tall this wants to be. See DewControls' note on intrinsic size. */
    int preferredHeight() const
    {
        return tokens::size::controlHeight;
    }

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

    /** A dropdown states focus by turning its BORDER accent rather than by
        adding a ring - DewLookAndFeel::drawComboBox - and that is the same
        statement, so it answers to the same rule. See DewButton::focusGained. */
    void focusGained (FocusChangeType cause) override
    {
        focus::noteFocusChange (cause);
        juce::ComboBox::focusGained (cause);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewDropdown)
};

// -----------------------------------------------------------------------------

} // namespace dew
