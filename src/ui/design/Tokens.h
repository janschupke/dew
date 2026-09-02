#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The design system's vocabulary.

    Every colour, spacing, radius and font size in dew comes from here. The point
    is not tidiness: it is that the step grid, the piano roll, the playlist and
    the mixer are four separately-written components that have to read as one
    application, and they only do that if they are quoting the same values rather
    than each choosing their own.

    Colours are named for their ROLE, not their appearance, so a theme change is
    a change here and nowhere else.
*/
namespace dew::tokens
{

// --- surfaces ----------------------------------------------------------------
namespace colour
{
    // Backgrounds, darkest to lightest.
    inline const juce::Colour wellDeep      { 0xff0e1013 };  ///< inside grids and timelines
    inline const juce::Colour well          { 0xff121417 };  ///< recessed areas
    inline const juce::Colour background    { 0xff17191d };  ///< window
    inline const juce::Colour surface       { 0xff22252b };  ///< panels, headers, strips
    inline const juce::Colour surfaceRaised { 0xff2b2f36 };  ///< controls at rest
    inline const juce::Colour surfaceHover  { 0xff343941 };  ///< controls under the cursor

    // Lines.
    inline const juce::Colour divider       { 0xff2c3037 };  ///< ordinary grid lines
    inline const juce::Colour dividerStrong { 0xff3d434d };  ///< bar lines, section edges
    inline const juce::Colour outline       { 0xff454c57 };  ///< control borders

    // Text.
    inline const juce::Colour textPrimary   { 0xffe6e8ec };
    inline const juce::Colour textSecondary { 0xff9aa2ae };
    inline const juce::Colour textDisabled  { 0xff5d646e };
    inline const juce::Colour textOnAccent  { 0xff10151c };

    // Meaning.
    inline const juce::Colour accent        { 0xff4fa3ff };  ///< selection, focus, primary action
    inline const juce::Colour accentMuted   { 0xff2f6ba8 };
    inline const juce::Colour playhead      { 0xffffc857 };
    inline const juce::Colour recording     { 0xffe4572e };
    inline const juce::Colour success       { 0xff3ecf8e };
    inline const juce::Colour warning       { 0xfff2c14e };
    inline const juce::Colour danger        { 0xffe4572e };

    // Step grid shading.
    inline const juce::Colour beatShade     { 0xff1b1e23 };  ///< every other beat
    inline const juce::Colour barShade      { 0xff20242b };  ///< first beat of a bar

    /** The channel colour ramp. Channels cycle through these so a new channel is
        immediately distinguishable from its neighbours without anyone choosing.
    */
    inline const juce::Colour channelRamp[] = {
        juce::Colour (0xffe4572e), juce::Colour (0xff29a19c), juce::Colour (0xff4fa3ff),
        juce::Colour (0xfff2c14e), juce::Colour (0xffb388eb), juce::Colour (0xff3ecf8e),
        juce::Colour (0xffff7eb6), juce::Colour (0xff76c7c0),
    };

    inline juce::Colour channelColour (int index)
    {
        constexpr int count = (int) (sizeof (channelRamp) / sizeof (channelRamp[0]));
        return channelRamp[((index % count) + count) % count];
    }
}

// --- spacing -----------------------------------------------------------------
/** A 4px base scale. Every gap, inset and margin is one of these, so vertical
    rhythm stays consistent across components nobody wrote at the same time.
*/
namespace space
{
    inline constexpr int xxs = 2;
    inline constexpr int xs  = 4;
    inline constexpr int sm  = 6;
    inline constexpr int md  = 8;
    inline constexpr int lg  = 12;
    inline constexpr int xl  = 16;
    inline constexpr int xxl = 24;
}

// --- shape -------------------------------------------------------------------
namespace radius
{
    inline constexpr float sm  = 3.0f;
    inline constexpr float md  = 5.0f;
    inline constexpr float lg  = 8.0f;
    inline constexpr float pill = 999.0f;
}

namespace stroke
{
    inline constexpr float hairline = 1.0f;
    inline constexpr float regular  = 1.5f;
    inline constexpr float bold     = 2.0f;
}

// --- type --------------------------------------------------------------------
namespace type
{
    inline constexpr float caption = 10.0f;   ///< knob labels, ruler numbers
    inline constexpr float small   = 11.0f;   ///< strip names, secondary text
    inline constexpr float body    = 13.0f;   ///< default
    inline constexpr float title   = 15.0f;   ///< panel headings
    inline constexpr float display = 20.0f;

    juce::Font font (float height, bool bold = false);
    juce::Font monospaced (float height);
}

// --- sizing ------------------------------------------------------------------
namespace size
{
    inline constexpr int controlHeight   = 26;  ///< buttons, combo boxes, number fields
    inline constexpr int controlHeightSm = 20;
    inline constexpr int iconButton      = 24;
    inline constexpr int knob            = 44;
    inline constexpr int knobSm          = 26;  ///< a knob on a row, drawn without its caption
    inline constexpr int rowHeight       = 34;  ///< channel rack and playlist rows
    inline constexpr int headerWidth     = 264; ///< the channel rack's header column
    inline constexpr int rulerHeight     = 22;
    inline constexpr int minTouchTarget  = 20;  ///< nothing clickable smaller than this
}

// --- motion ------------------------------------------------------------------
namespace motion
{
    inline constexpr int uiRefreshHz    = 30;  ///< list and panel refreshes
    inline constexpr int playheadHz     = 60;  ///< anything tracking the transport

    /** Durations, in milliseconds. Short enough that nothing feels laggy, long
        enough that a change reads as movement rather than as a jump cut.
    */
    inline constexpr int quickMs        = 90;   ///< hover and press feedback
    inline constexpr int popupMs        = 130;  ///< menus and dropdowns opening
    inline constexpr int panelMs        = 180;  ///< larger surfaces sliding in

    /** How far a popup rises as it fades in. */
    inline constexpr int popupRisePx    = 6;
}

} // namespace dew::tokens
