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

    /** A piano keyboard's two key colours. Not "black" and "white": a black key
        is a dark surface and a white key is a light one, and calling them what
        they ARE is what lets a light theme swap them here rather than in the
        piano roll's painter.
    */
    inline const juce::Colour keyBlack      { 0xff1c1f24 };
    inline const juce::Colour keyWhite      { 0xffd8dce3 };

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

// --- emphasis ----------------------------------------------------------------
/** How strongly something is stated.

    dew had a second, undeclared design system: fifteen alpha values and nine
    brighten factors, each chosen on its own, which between them made a
    twenty-four rung scale nobody had named. Two panels drawn "faintly" were
    drawn at 0.07 and 0.10, a week apart, and neither author knew about the
    other.

    Named for what a value MEANS, so a component asks for a wash rather than for
    0.20 and the two cannot drift apart again.
*/
namespace emphasis
{
    // Alphas, faintest to strongest.
    inline constexpr float tint    = 0.08f;  ///< an accent behind a selected region
    inline constexpr float wash    = 0.20f;  ///< a rubber band, a playhead's column
    inline constexpr float hatch   = 0.25f;  ///< the lines of an inert area
    inline constexpr float subdued = 0.35f;  ///< past the end, another channel, muted
    inline constexpr float dimmed  = 0.55f;  ///< a stopped playhead, a scrim
    inline constexpr float strong  = 0.85f;  ///< nearly opaque

    /** How much lighter a surface gets when it is being touched.

        A large surface needs a subtler lift than a small one, or a hovered
        mixer strip flares while a hovered button barely moves. That distinction
        is real and is why there were 0.05 and 0.06 for strips and cards and
        0.10 for buttons; the other six values were not.
    */
    inline constexpr float surfaceLift = 0.06f;  ///< a strip, a card, a row
    inline constexpr float controlLift = 0.10f;  ///< a button under the cursor
    inline constexpr float pressLift   = 0.22f;  ///< a button being held
    inline constexpr float edgeLift    = 0.35f;  ///< a note's border against its own fill

    /** The same statement made of a colour rather than of an alpha.

        Three of dew's surfaces say "this is here but not sounding" by draining
        a colour rather than by fading it, because a faded clip on a dark well
        disappears while a drained one still reads as a clip. That was written
        out three times as `.withSaturation (0.1f).withMultipliedBrightness
        (0.6f)`, and a fourth idea of "inactive" - 0.3 and 0.7 - lived in the
        button primitive.
    */
    inline juce::Colour silenced (juce::Colour c)
    {
        return c.withSaturation (0.1f).withMultipliedBrightness (0.6f);
    }

    /** A control that cannot be used. Weaker than `silenced`: a disabled button
        must still read as a button, where a muted clip may recede. */
    inline juce::Colour disabled (juce::Colour c)
    {
        return c.withMultipliedSaturation (0.3f).withMultipliedBrightness (0.7f);
    }

    /** Present, usable, and not the one being talked about - a step off the
        base pitch, a pattern that is not the current one. Hue intact so it is
        still recognisably the same thing. */
    inline juce::Colour secondary (juce::Colour c)
    {
        return c.withSaturation (0.3f);
    }
}

// --- spacing -----------------------------------------------------------------
/** A modular spacing scale: 2, 4, 6, 8, 12, 16, 24.

    Not a strict 4px grid, and deliberately not. `sm` and `lg` are half-steps,
    because a control surface this dense needs a gap between "touching" and
    "separated" that a 4/8/16 ladder does not have. Renumbering them would move
    every layout in the application by two pixels to satisfy a rule nothing
    actually wanted.

    Every gap, inset and margin is one of these seven, and there is a test that
    says so.
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
    /** The smallest rounding that still reads as rounded on something four
        pixels tall - a note, a clip, a meter bar. Seven sites had reached for
        1.5 or 2.0 by hand because sm was visibly too round at that size. */
    inline constexpr float xs  = 2.0f;

    inline constexpr float sm  = 3.0f;
    inline constexpr float md  = 5.0f;
    inline constexpr float lg  = 8.0f;   ///< a dialog: a bigger surface rounds more

    // `pill` is retired. Nothing in a DAW is a pill, it had no references, and
    // an unused token is a claim the code does not back.
}

namespace stroke
{
    /** A hatch line, and the half-pixel inset that puts a one-pixel edge ON a
        pixel rather than across two. */
    inline constexpr float whisper  = 0.5f;

    inline constexpr float hairline = 1.0f;

    /** The same weight for JUCE's integer overloads - drawRect on an integer
        rectangle takes an int, and a cast at the call site would only be
        hiding that. */
    inline constexpr int hairlinePx = 1;
    inline constexpr float regular  = 1.5f;
    inline constexpr float bold     = 2.0f;
}

/** Stroke weights in the icons' own 0..1 space.

    Icons.cpp passed eleven different thicknesses to its stroke helpers, which
    is why the loop arrow and the undo arrow, drawn a week apart, did not look
    like the same family.
*/
namespace icon
{
    inline constexpr float hair    = 0.07f;  ///< a tick or a fine rule
    inline constexpr float regular = 0.10f;  ///< the default
    inline constexpr float bold    = 0.13f;  ///< a stem that must read at 16px
    inline constexpr float ring    = 0.26f;  ///< the loop arrow's ring
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
    inline constexpr int rulerHeight     = 22;
    inline constexpr int minTouchTarget  = 20;  ///< nothing clickable smaller than this

    /** Horizontal strips, shortest to tallest.

        Six of these were declared in six files with nothing relating them: the
        toolbars said 34, the tab bar 30, the transport bar 46, the status bar
        24, the effect chain's heading 26 and a settings row 28. They are one
        ladder, and they read as one only if they are declared as one.
    */
    inline constexpr int stripStatus    = 24;         ///< the status line
    inline constexpr int stripHeading   = 26;         ///< a section heading with a button on it
    inline constexpr int stripFormRow   = 28;         ///< a settings row: a control plus room
    inline constexpr int stripTabs      = 30;         ///< the editor tab bar
    inline constexpr int stripToolbar   = rowHeight;  ///< an editor's toolbar IS a row
    inline constexpr int stripTransport = 46;         ///< the one strip that is loud

    static_assert (stripFormRow >= controlHeight, "a form row must hold a control");
    static_assert (stripToolbar >= controlHeight, "a toolbar must hold a control");
    static_assert (stripHeading >= controlHeight, "a heading must hold its button");

    /** Gutters: the fixed column beside a scrolling timeline.

        Three of them, of which one was a token and two were not.
    */
    inline constexpr int gutterChannel  = 264;  ///< the channel rack's header column
    inline constexpr int gutterTrack    = 156;  ///< the playlist's track headers
    inline constexpr int gutterKeyboard = 54;   ///< the piano roll's key strip
    inline constexpr int gutterLabel    = 76;   ///< a settings form's label column

    /** Declared identically in three components, none of which knew. */
    inline constexpr int scrollThickness = 10;

    /** A captioned knob. DewKnob hard-coded 13 and 14 in three places, and six
        call sites independently spelled 68 for a row holding one. */
    inline constexpr int knobCaption = 13;
    inline constexpr int knobValue   = 14;
    inline constexpr int knobRow     = 68;

    static_assert (knobRow >= knobCaption + knobSm + knobValue,
                   "a knob row must hold a compact knob and both its labels");

    inline constexpr int letterToggle  = 22;  ///< the M and S on a row
    inline constexpr int meterHeight   = 10;
    inline constexpr int waveformInset = 2;   ///< was -2, -2 and -3 in three painters
}

// --- motion ------------------------------------------------------------------
namespace motion
{
    inline constexpr int uiRefreshHz    = 30;  ///< list and panel refreshes
    inline constexpr int playheadHz     = 60;  ///< anything tracking the transport

    /** Durations, in milliseconds. Short enough that nothing feels laggy, long
        enough that a change reads as movement rather than as a jump cut.
    */
    inline constexpr int selectMs       = 70;   ///< a selection, a playhead's state
    inline constexpr int quickMs        = 90;   ///< hover and press feedback
    inline constexpr int valueMs        = 120;  ///< a knob catching up with the document
    inline constexpr int popupMs        = 130;  ///< menus and dropdowns opening
    inline constexpr int panelMs        = 180;  ///< larger surfaces sliding in

    /** How far a popup rises as it fades in. */
    inline constexpr int popupRisePx    = 6;

    /** How long typing has to stop before the editor acts on what was typed.

        Long enough that it does not run mid-word, short enough that a
        diagnostic feels like a reaction rather than a report.
    */
    inline constexpr int typingPauseMs  = 250;

    /** How fast a meter falls, as a TIME constant rather than a per-tick
        coefficient.

        dew's three meters each multiplied by 0.82 or 0.8 per tick, which locks
        a widget to the rate it was tuned at - SignalScope says so in a comment
        rather than fixing it. A time constant reads the same at 30Hz and at
        60Hz, and at a dropped frame.
    */
    inline constexpr int meterReleaseMs = 320;
}

} // namespace dew::tokens
