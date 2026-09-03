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

    int getPopupMenuBorderSize() override;

    /** Fades and lifts a menu as it opens. JUCE gives no hook for the close, so
        this is deliberately one-directional rather than half an animation
        pretending to be a whole one.
    */
    void preparePopupMenuWindow (juce::Component&) override;
};

} // namespace dew
