#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "design/Tokens.h"

namespace dew
{

/** The original colour names, now aliases onto the design tokens.

    Kept so the editors that were written before the design system existed keep
    compiling while they are migrated one at a time. There is only ONE
    definition of each colour - in tokens - so the two cannot drift.

    New code should use dew::tokens::colour directly.
*/
namespace Palette
{
    inline const auto& background  = tokens::colour::background;
    inline const auto& panel       = tokens::colour::surface;
    inline const auto& panelDark   = tokens::colour::well;
    inline const auto& line        = tokens::colour::divider;
    inline const auto& lineStrong  = tokens::colour::dividerStrong;
    inline const auto& text        = tokens::colour::textPrimary;
    inline const auto& textDim     = tokens::colour::textSecondary;
    inline const auto& accent      = tokens::colour::accent;
    inline const auto& playhead    = tokens::colour::playhead;
    inline const auto& beat        = tokens::colour::beatShade;
}

/** Styling for the stock JUCE controls dew still uses - combo boxes, labels,
    scrollbars, menus. The dew primitives paint themselves and do not depend on
    this being installed.
*/
class DewLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DewLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;

    /** Tabs fell through to LookAndFeel_V4, which meant no dew hover treatment
        and a tab bar that did not look like the rest of the application.
    */
    void drawTabButton (juce::TabBarButton&, juce::Graphics&,
                        bool isMouseOver, bool isMouseDown) override;

    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;

    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&,
                                       int width, int height) override;

    // --- combo boxes and menus ----------------------------------------------
    // These had no overrides at all, so a dropdown was a stock JUCE widget
    // sitting next to hand-painted dew primitives.
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                    int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    int getPopupMenuBorderSize() override;

    /** Fades and lifts a menu as it opens. JUCE gives no hook for the close, so
        this is deliberately one-directional rather than half an animation
        pretending to be a whole one.
    */
    void preparePopupMenuWindow (juce::Component&) override;
};

} // namespace dew
