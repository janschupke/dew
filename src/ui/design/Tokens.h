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

// clang-format off
// --- surfaces ----------------------------------------------------------------
namespace colour
{
    /** Every colour dew paints, as one object so a theme is one assignment.

        The names below are REFERENCES into `active`, not values, which is what
        lets a theme change nothing at the 437 places that read one. A reference
        binds to an address at compile time and reads whatever is there when it
        is used, so `g.setColour (colour::accent)` and
        `colour::accent.withAlpha (…)` both keep working and both follow the
        theme. The values are still initialised dynamically, exactly as the
        constants they replaced were - nothing here may be read during static
        initialisation, and nothing does.

        What a theme may NOT change is `channelRamp`. That is document data:
        entityColour writes it into every .dew file, dew_model restates it as
        strings, and the colour picker offers it. Repainting it would make every
        saved project disagree with the swatch it was chosen from. What varies
        is `textOnAccent`, drawn on top of it.
    */
    struct Palette
    {
        // Backgrounds, darkest to lightest.
        juce::Colour wellDeep;      ///< inside grids and timelines
        juce::Colour well;          ///< recessed areas
        juce::Colour background;    ///< window
        juce::Colour surface;       ///< panels, headers, strips
        juce::Colour surfaceRaised; ///< controls at rest
        juce::Colour surfaceHover;  ///< controls under the cursor

        // Lines.
        juce::Colour divider;       ///< ordinary grid lines
        juce::Colour dividerStrong; ///< bar lines, section edges
        juce::Colour outline;       ///< control borders

        // Text.
        juce::Colour textPrimary;
        juce::Colour textSecondary;
        juce::Colour textDisabled;
        juce::Colour textOnAccent;

        // Meaning.
        juce::Colour accent;      ///< selection, focus, primary action
        juce::Colour accentMuted;
        juce::Colour playhead;
        juce::Colour recording;
        juce::Colour success;
        juce::Colour warning;
        juce::Colour danger;

        /** What a control DOES, as a colour.

            One hue per audio function, so a level reads as a level and an
            envelope stage reads as an envelope stage wherever either appears.
            Before this every knob in dew painted the same `accent` arc: on a
            34px channel row and a 72px mixer strip there is no caption, so
            volume and pan were told apart only by one of them filling from the
            centre.

            Deliberately QUIETER than channelRamp. The ramp means identity -
            which channel this is - and identity has to win, because on the
            playlist a clip's identity colour and an automation curve's function
            colour are inches apart. Function is chrome; identity is content.

            The `func` prefix is not decoration. "every token the design system
            declares is one the app uses" matches whole words across the tree,
            and bare `level`, `time` and `space` would be satisfied by a mixer
            strip's local variable and by `space::md` - a gate that passes for
            the wrong reason is not a gate.
        */
        juce::Colour funcTone;       ///< spectral shaping
        juce::Colour funcTime;       ///< envelope in time
        juce::Colour funcLevel;      ///< how loud
        juce::Colour funcStereo;     ///< where in the field
        juce::Colour funcSpace;      ///< ambience and echo
        juce::Colour funcModulation; ///< what makes it move
        juce::Colour funcPitch;      ///< which note you hear

        /** A piano keyboard's two key colours. Not "black" and "white": a black
            key is a dark surface and a white key is a light one, and calling
            them what they ARE is what lets a theme swap them here rather than
            in the piano roll's painter.
        */
        juce::Colour keyBlack;
        juce::Colour keyWhite;

        // Step grid shading.
        juce::Colour beatShade; ///< every other beat
        juce::Colour barShade;  ///< first beat of a bar
    };

    /** dew as it has always looked. */
    Palette darkPalette();

    /** The same roles, pushed apart until every pair a person reads clears 7:1
        and every edge they have to find clears 4.5:1 - one grade above the AA
        the default palette is held to.

        Still DARK. A light theme is a different job: emphasis::silenced and
        emphasis::disabled both multiply brightness downward, the four lift
        rungs mean "how much brighter", and wellDeep is used as a scrim at four
        sites. Every one of those stays correct here and would invert there.
    */
    Palette highContrastPalette();

    /** The palette in force. Assigned by theme::apply on the message thread,
        and read by everything below. */
    inline Palette active = darkPalette();

    inline const juce::Colour& wellDeep = active.wellDeep;
    inline const juce::Colour& well = active.well;
    inline const juce::Colour& background = active.background;
    inline const juce::Colour& surface = active.surface;
    inline const juce::Colour& surfaceRaised = active.surfaceRaised;
    inline const juce::Colour& surfaceHover = active.surfaceHover;

    inline const juce::Colour& divider = active.divider;
    inline const juce::Colour& dividerStrong = active.dividerStrong;
    inline const juce::Colour& outline = active.outline;

    inline const juce::Colour& textPrimary = active.textPrimary;
    inline const juce::Colour& textSecondary = active.textSecondary;
    inline const juce::Colour& textDisabled = active.textDisabled;
    inline const juce::Colour& textOnAccent = active.textOnAccent;

    inline const juce::Colour& accent = active.accent;
    inline const juce::Colour& accentMuted = active.accentMuted;
    inline const juce::Colour& playhead = active.playhead;
    inline const juce::Colour& recording = active.recording;
    inline const juce::Colour& success = active.success;
    inline const juce::Colour& warning = active.warning;
    inline const juce::Colour& danger = active.danger;

    inline const juce::Colour& funcTone = active.funcTone;
    inline const juce::Colour& funcTime = active.funcTime;
    inline const juce::Colour& funcLevel = active.funcLevel;
    inline const juce::Colour& funcStereo = active.funcStereo;
    inline const juce::Colour& funcSpace = active.funcSpace;
    inline const juce::Colour& funcModulation = active.funcModulation;
    inline const juce::Colour& funcPitch = active.funcPitch;

    inline const juce::Colour& keyBlack = active.keyBlack;
    inline const juce::Colour& keyWhite = active.keyWhite;

    inline const juce::Colour& beatShade = active.beatShade;
    inline const juce::Colour& barShade = active.barShade;

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

// clang-format on
// --- emphasis ----------------------------------------------------------------
/** How strongly something is stated.

    dew had a second, undeclared design system: fifteen alpha values and nine
    brighten factors, each chosen on its own, which between them made a
    twenty-four rung scale nobody had named. Two panels drawn "faintly" were
    drawn at 0.07 and 0.10, a week apart, and neither author knew about the
    other.

// clang-format off
    Named for what a value MEANS, so a component asks for a wash rather than for
    0.20 and the two cannot drift apart again.
*/
namespace emphasis
{
// Alphas, faintest to strongest.
inline constexpr float tint = 0.08f;    ///< an accent behind a selected region
inline constexpr float wash = 0.20f;    ///< a rubber band, a playhead's column
inline constexpr float hatch = 0.25f;   ///< the lines of an inert area
inline constexpr float subdued = 0.35f; ///< past the end, another channel, muted
inline constexpr float dimmed = 0.55f;  ///< a stopped playhead, a scrim
inline constexpr float strong = 0.85f;  ///< nearly opaque

/** How much lighter a surface gets when it is being touched.

    A large surface needs a subtler lift than a small one, or a hovered
    mixer strip flares while a hovered button barely moves. That distinction
    is real and is why there were 0.05 and 0.06 for strips and cards and
    0.10 for buttons; the other six values were not.
*/
inline constexpr float surfaceLift = 0.06f; ///< a strip, a card, a row
inline constexpr float controlLift = 0.10f; ///< a button under the cursor
inline constexpr float pressLift = 0.22f;   ///< a button being held
inline constexpr float edgeLift = 0.35f;    ///< a note's border against its own fill

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
} // namespace emphasis

// clang-format on
// --- spacing -----------------------------------------------------------------
/** A modular spacing scale: 2, 4, 6, 8, 12, 16, 24.

    Not a strict 4px grid, and deliberately not. `sm` and `lg` are half-steps,
    because a control surface this dense needs a gap between "touching" and
    "separated" that a 4/8/16 ladder does not have. Renumbering them would move
    every layout in the application by two pixels to satisfy a rule nothing
    actually wanted.

// clang-format off
    Every gap, inset and margin is one of these seven, and there is a test that
    says so.
*/
namespace space
{
inline constexpr int xxs = 2;
inline constexpr int xs = 4;
inline constexpr int sm = 6;
inline constexpr int md = 8;
inline constexpr int lg = 12;
inline constexpr int xl = 16;
inline constexpr int xxl = 24;
} // namespace space

// --- shape -------------------------------------------------------------------
namespace radius
{
/** The smallest rounding that still reads as rounded on something four
    pixels tall - a note, a clip, a meter bar. Seven sites had reached for
    1.5 or 2.0 by hand because sm was visibly too round at that size. */
inline constexpr float xs = 2.0f;

inline constexpr float sm = 3.0f;
inline constexpr float md = 5.0f;
inline constexpr float lg = 8.0f; ///< a dialog: a bigger surface rounds more

// `pill` is retired. Nothing in a DAW is a pill, it had no references, and
// an unused token is a claim the code does not back.
} // namespace radius

namespace stroke
{
/** A hatch line, and the half-pixel inset that puts a one-pixel edge ON a
    pixel rather than across two. */
inline constexpr float whisper = 0.5f;

inline constexpr float hairline = 1.0f;

/** The same weight for JUCE's integer overloads - drawRect on an integer
    rectangle takes an int, and a cast at the call site would only be
    hiding that. */
inline constexpr int hairlinePx = 1;
inline constexpr float regular = 1.5f;
inline constexpr float bold = 2.0f;
} // namespace stroke

// clang-format on
/** Stroke weights in the icons' own 0..1 space.

// clang-format off
    Icons.cpp passed eleven different thicknesses to its stroke helpers, which
    is why the loop arrow and the undo arrow, drawn a week apart, did not look
    like the same family.
*/
namespace icon
{
inline constexpr float hair = 0.07f;    ///< a tick or a fine rule
inline constexpr float regular = 0.10f; ///< the default
inline constexpr float bold = 0.13f;    ///< a stem that must read at 16px
inline constexpr float ring = 0.26f;    ///< the loop arrow's ring
} // namespace icon

// --- type --------------------------------------------------------------------
namespace type
{
/** The floor of this ladder used to be 10 and 11, which is a size you read
    by leaning in. A control surface is dense, but nothing here is so dense
    that a caption had to be smaller than the smallest comfortable size -
    and the two smallest rungs are the ones almost every label in the
    application lands on.
*/
inline constexpr float caption = 11.0f; ///< knob labels, ruler numbers
inline constexpr float small = 12.0f;   ///< strip names, secondary text
inline constexpr float body = 13.0f;    ///< default
inline constexpr float title = 15.0f;   ///< panel headings
inline constexpr float display = 20.0f;

/** The score tab's own rungs.

    A document is READ, for minutes at a time; the rest of the application
    is glanced at. That is a different job from the one the scale above
    does, and it is the only text in dew whose size the reader chooses -
    so it gets four rungs of its own rather than borrowing four that were
    picked to make a dense panel legible.
*/
inline constexpr float codeSmall = 12.0f;
inline constexpr float codeBody = 14.0f; ///< what the score tab opens at
inline constexpr float codeLarge = 17.0f;
inline constexpr float codeHuge = 21.0f;

juce::Font font (float height, bool bold = false);
juce::Font monospaced (float height);
} // namespace type

// --- sizing ------------------------------------------------------------------
namespace size
{
inline constexpr int controlHeight = 26; ///< buttons, combo boxes, number fields
inline constexpr int controlHeightSm = 20;
inline constexpr int iconButton = 24;

/** The column a ROW spends on a leading glyph, and the mark inside it.

    A menu item, a dropdown option and a button with a picture beside its word
    all reserve the same column, because a dropdown's closed box sits directly
    on top of the list it opens and a glyph that moved between the two would
    read as the list jumping.
*/
inline constexpr int glyphColumn = 16;
inline constexpr int glyphMark = 14;

static_assert (glyphMark <= glyphColumn, "a glyph has to fit the column it is given");
inline constexpr int knob = 44;
inline constexpr int knobSm = 26;    ///< a knob on a row, drawn without its caption
inline constexpr int rowHeight = 34; ///< channel rack and playlist rows
inline constexpr int rulerHeight = 22;
inline constexpr int minTouchTarget = 24; ///< nothing clickable smaller than this

/** Horizontal strips, shortest to tallest.

    Six of these were declared in six files with nothing relating them: the
    toolbars said 34, the tab bar 30, the transport bar 46, the status bar
    24, the effect chain's heading 26 and a settings row 28. They are one
    ladder, and they read as one only if they are declared as one.
*/
inline constexpr int stripStatus = 24;         ///< the status line
inline constexpr int stripHeading = 26;        ///< a section heading with a button on it
inline constexpr int stripFormRow = 28;        ///< a settings row: a control plus room
inline constexpr int stripTabs = 30;           ///< the editor tab bar
inline constexpr int stripToolbar = rowHeight; ///< an editor's toolbar IS a row
inline constexpr int stripTransport = 46;      ///< the one strip that is loud

static_assert (stripFormRow >= controlHeight, "a form row must hold a control");
static_assert (stripToolbar >= controlHeight, "a toolbar must hold a control");
static_assert (stripHeading >= controlHeight, "a heading must hold its button");

/** Gutters: the fixed column beside a scrolling timeline.

    Three of them, of which one was a token and two were not.
*/
inline constexpr int gutterChannel = 264; ///< the channel rack's header column
inline constexpr int gutterTrack = 156;   ///< the playlist's track headers
inline constexpr int gutterKeyboard = 54; ///< the piano roll's key strip
inline constexpr int gutterLabel = 76;    ///< a settings form's label column

/** Declared identically in three components, none of which knew. */
inline constexpr int scrollThickness = 10;

/** A captioned knob. DewKnob hard-coded 13 and 14 in three places, and six
    call sites independently spelled 68 for a row holding one. */
inline constexpr int knobCaption = 15;
inline constexpr int knobValue = 16;
inline constexpr int knobRow = 68;

static_assert (knobRow >= knobCaption + knobSm + knobValue,
               "a knob row must hold a compact knob and both its labels");

/** How tall ONE playlist lane is: a range, not a rung.

    Header and lane are the same height by construction, which is the only
    reason the two cannot drift. The range exists because an automation
    curve drawn into a 34px lane has a 28px value axis, and the 7px grab
    radius covers a fifth of it - the point editor is decorative at that
    size, and there was no way to make the lane any taller.

    Multiples of the rung rather than bare numbers, so a change to rowHeight
    carries the whole range with it. Declared HERE and nowhere else: a
    component restating one of these would be exactly the duplication the
    ladder gate exists to catch.
*/
inline constexpr int trackHeightMin = rowHeight;     ///< today's row, and the densest readable one
inline constexpr int trackHeightDefault = rowHeight; ///< so nothing re-flows on upgrade
inline constexpr int trackHeightRoomy = rowHeight
                                        * 2; ///< past here a header has room for a second line
inline constexpr int trackHeightMax = rowHeight * 6; ///< an arrangement, not one lane

/** How tall ONE piano-roll pitch row is: a range, like a lane's.

    The roll had a fixed 14, which is a good density for writing a melody
    and a bad one for reading a chord voicing across four octaves - and
    there was no way to change it, in the one view whose vertical axis is
    the material rather than a list.

    Not multiples of a rung: a pitch row is not a list row, and the numbers
    that make a note readable have nothing to do with the ones that make a
    channel header hold a knob.
*/
inline constexpr int pianoRowMin = 8;      ///< the densest a note still reads at
inline constexpr int pianoRowDefault = 14; ///< so nothing re-flows on upgrade
inline constexpr int pianoRowRoomy = 20;   ///< past here a key strip fits its note names
inline constexpr int pianoRowMax = 40;

static_assert (pianoRowMin < pianoRowRoomy && pianoRowRoomy < pianoRowMax,
               "the roomy threshold has to sit inside the range");
static_assert (pianoRowMin <= pianoRowDefault && pianoRowDefault <= pianoRowMax,
               "the default has to be reachable");

inline constexpr int letterToggle = 24; ///< a toggle on a row: R, and the on/off indicator
inline constexpr int meterHeight = 10;

static_assert (trackHeightMin >= letterToggle + 2 * space::xs,
               "a lane must hold its on/off indicator with room around it");
static_assert (trackHeightMin < trackHeightRoomy && trackHeightRoomy < trackHeightMax,
               "the roomy threshold has to sit inside the range");
static_assert (trackHeightMin <= trackHeightDefault && trackHeightDefault <= trackHeightMax,
               "the default has to be reachable");
inline constexpr int waveformInset = 2; ///< was -2, -2 and -3 in three painters

/** One mixer insert's column.

    The only VERTICAL strip in an application whose other strips are horizontal,
    which is why it is a width and sits apart from the ladder above rather than
    inside it. 96 was chosen when the mixer held four inserts and nothing could
    change that; a mixer is read across, so the number that matters is how many
    fit at once, and it now has to hold as many as somebody adds.
*/
inline constexpr int mixerStripWidth = 72;

static_assert (mixerStripWidth >= knobSm + 2 * space::md + 2 * space::sm,
               "a strip must hold its pan knob and the insets around it");
// There was a second assert here - "a strip must hold M and S side by side" -
// and its claim went with the pair. One indicator needs 24 plus its insets,
// which the pan knob above already demands more than, so restating it would be
// an assert that can never fail: noise where a claim used to be.
} // namespace size

// --- motion ------------------------------------------------------------------
namespace motion
{
inline constexpr int uiRefreshHz = 30; ///< list and panel refreshes
inline constexpr int playheadHz = 60;  ///< anything tracking the transport

/** Durations, in milliseconds. Short enough that nothing feels laggy, long
    enough that a change reads as movement rather than as a jump cut.
*/
inline constexpr int selectMs = 70; ///< a selection, a playhead's state
inline constexpr int quickMs = 90;  ///< hover and press feedback
inline constexpr int valueMs = 120; ///< a knob catching up with the document
inline constexpr int popupMs = 130; ///< menus and dropdowns opening
inline constexpr int panelMs = 180; ///< larger surfaces sliding in

/** How far a popup rises as it fades in. */
inline constexpr int popupRisePx = 6;

/** How long typing has to stop before the editor acts on what was typed.

    Long enough that it does not run mid-word, short enough that a
    diagnostic feels like a reaction rather than a report.
*/
inline constexpr int typingPauseMs = 250;

/** How fast a meter falls, as a TIME constant rather than a per-tick
    coefficient.

    dew's three meters each multiplied by 0.82 or 0.8 per tick, which locks
    a widget to the rate it was tuned at - SignalScope says so in a comment
    rather than fixing it. A time constant reads the same at 30Hz and at
    60Hz, and at a dropped frame.
*/
inline constexpr int meterReleaseMs = 320;
} // namespace motion

// clang-format on
} // namespace dew::tokens
