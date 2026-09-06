#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

namespace dew
{

/** Styling for the stock JUCE controls dew still uses - combo boxes, labels,
    scrollbars, menus. The dew primitives paint themselves and do not depend on
    this being installed.
*/
class DewLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DewLookAndFeel();

    /** Seeds every JUCE ColourId from the palette in force.

        Called by the constructor and again whenever the theme changes. It has
        to be a second entry point rather than only a constructor: these are
        one-time copies into the LookAndFeel, so a palette swapped underneath
        them changes nothing until they are taken again.
    */
    void applyPalette();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    // --- type ---------------------------------------------------------------
    // Six font paths used to fall through to LookAndFeel_V2/V4 and land on
    // sizes that exist nowhere in tokens::type - 10.8 on an 18px mixer button,
    // 17 in a menu, 13 BOLD on every tooltip. Each of these closes one.
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getSliderPopupFont (juce::Slider&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    /** JUCE hard-codes 13pt bold inside drawTooltip, so the only way onto the
        scale is to draw the tooltip ourselves.
    */
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    /** The Label a Slider makes for its own text box never gets a setFont, so
        it kept JUCE's 15pt default inside a 15px box.
    */
    juce::Label* createSliderTextBox (juce::Slider&) override;

    /** A slider's text box is an INPUT, and it was the one input in dew that
        nothing here painted: V2 draws a square one-pixel drawRect, so the
        number between a stepper's + and - was the only box in the window that
        did not match the two buttons touching it. Every other Label falls
        through to the base, because a label that is not inside a Slider is
        text rather than a control.
    */
    void drawLabel (juce::Graphics&, juce::Label&) override;

    /** And the editor a text box opens when it is typed into, for the same
        reason: LookAndFeel_V4 draws that outline as a drawRect too, so a field
        went square-cornered the moment it was being edited. One override
        covers the slider's box and DewNumberField's typed edit, which is the
        other place a juce::TextEditor sits inside a dew input.
    */
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height,
                                   juce::TextEditor&) override;

    /** The + and - a Slider makes for IncDecButtons.

        JUCE hands back a plain TextButton, and a plain juce::Button completes a
        click for whichever mouse button pressed it - so right-clicking the
        octave stepper's + moved the oscillator up an octave and wrote an undo
        step for it. There is no other way in: the two buttons are children the
        slider creates for itself, so a DewSlider cannot refuse the press on
        their behalf and this hook is the only place that can.
    */
    juce::Button* createSliderButton (juce::Slider&, bool isIncrement) override;

    /** Tabs fell through to LookAndFeel_V4, which meant no dew hover treatment
        and a tab bar that did not look like the rest of the application.
    */
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver,
                        bool isMouseDown) override;

    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;

    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int width,
                                       int height) override;

    // --- combo boxes and menus ----------------------------------------------
    // These had no overrides at all, so a dropdown was a stock JUCE widget
    // sitting next to hand-painted dew primitives.
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX,
                       int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    /** Where a dropdown's menu opens. LookAndFeel_V2's version is why a menu
        covered the select it belongs to.
    */
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&,
                                                             juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator,
                            bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                    int standardMenuItemHeight, int& idealWidth,
                                    int& idealHeight) override;

    /** A category heading inside a menu, and the room it takes.

        The size hook exists only in its WithOptions form - JUCE declares no
        plain variant for a header the way it does for an item - so this is the
        one place the class reaches for that spelling.
    */
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&,
                                     const juce::String& sectionName) override;

    void getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text,
                                                        int standardMenuItemHeight, int& idealWidth,
                                                        int& idealHeight,
                                                        const juce::PopupMenu::Options&) override;

    int getPopupMenuBorderSize() override;

    /** Fades and lifts a menu as it opens. JUCE gives no hook for the close, so
        this is deliberately one-directional rather than half an animation
        pretending to be a whole one.
    */
    void preparePopupMenuWindow (juce::Component&) override;
};

} // namespace dew
