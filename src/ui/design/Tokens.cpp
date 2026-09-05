#include "ui/design/Tokens.h"

namespace dew::tokens::colour
{

// clang-format off

Palette darkPalette()
{
    return {
        .wellDeep = juce::Colour (0xff0e1013),
        .well = juce::Colour (0xff121417),
        .background = juce::Colour (0xff17191d),
        .surface = juce::Colour (0xff22252b),
        .surfaceRaised = juce::Colour (0xff2b2f36),
        .surfaceHover = juce::Colour (0xff343941),

        .divider = juce::Colour (0xff2c3037),
        .dividerStrong = juce::Colour (0xff3d434d),
        .outline = juce::Colour (0xff7c838e),

        .textPrimary = juce::Colour (0xffe6e8ec),
        .textSecondary = juce::Colour (0xff9aa2ae),
        .textDisabled = juce::Colour (0xff858c96),
        .textOnAccent = juce::Colour (0xff10151c),

        .accent = juce::Colour (0xff4fa3ff),
        .accentMuted = juce::Colour (0xff4682bf),
        .playhead = juce::Colour (0xffffc857),
        .recording = juce::Colour (0xffff7a51),
        .success = juce::Colour (0xff3ecf8e),
        .warning = juce::Colour (0xfff2c14e),
        // Red, and no longer the record colour. Hue 0 at the highest saturation
        // that still clears 4.5:1 as text on surfaceHover: a deep red is too
        // dark to read on any of the six grounds, so danger is a LIGHT red.
        .danger = juce::Colour (0xffff7878),

        .funcTone = juce::Colour (0xffc9ab81),
        .funcTime = juce::Colour (0xffabbf7c),
        .funcLevel = juce::Colour (0xff80c4a0),
        .funcStereo = juce::Colour (0xff7fc7c7),
        .funcSpace = juce::Colour (0xff958fdb),
        .funcModulation = juce::Colour (0xffc88ad1),
        .funcPitch = juce::Colour (0xffd48ca4),

        .keyBlack = juce::Colour (0xff1c1f24),
        .keyWhite = juce::Colour (0xffd8dce3),

        .beatShade = juce::Colour (0xff1b1e23),
        .barShade = juce::Colour (0xff20242b),
    };
}

/*  Every value below is DERIVED, not chosen by eye.

    The surfaces were pushed down and apart first - wellDeep is black, and the
    six rungs keep the same order they have always had. Then each meaning and
    function colour kept its hue and saturation and had only its lightness
    raised, by bisection, until it cleared its target against surfaceHover: the
    lightest ground anything in dew is drawn on, so clearing it clears the other
    five. Targets are 7:1 for anything read and 4.5:1 for an edge or an arc.

    Keeping hue and saturation is the point. A high-contrast theme that also
    re-hued everything would be a second design to maintain; this one is the
    same design with the distances opened up, so funcTone is still the tone hue
    and the accent is still blue.

    Two values are unchanged from the dark palette - playhead and warning were
    already past 7:1.

    `recording` and `danger` used to be one colour in both palettes, which meant
    every trash glyph in the application was drawn in the record colour. They
    are two hues now: recording keeps the orange it has always had, and danger
    is red at hue 0. Both are raised the same way as everything else here - hue
    and saturation fixed, lightness raised by bisection - which at full
    brightness means saturation coming down rather than value going up.
*/
Palette highContrastPalette()
{
    return {
        .wellDeep = juce::Colour (0xff000000),
        .well = juce::Colour (0xff08090b),
        .background = juce::Colour (0xff0d0f12),
        .surface = juce::Colour (0xff16191e),
        .surfaceRaised = juce::Colour (0xff22262d),
        .surfaceHover = juce::Colour (0xff2e333c),

        .divider = juce::Colour (0xff454b55),
        .dividerStrong = juce::Colour (0xff6b727e),
        .outline = juce::Colour (0xff9aa2ae),

        .textPrimary = juce::Colour (0xffffffff),
        .textSecondary = juce::Colour (0xffd5dae1),
        .textDisabled = juce::Colour (0xffa6aeba),
        .textOnAccent = juce::Colour (0xff000000),

        .accent = juce::Colour (0xff8fc5ff),
        .accentMuted = juce::Colour (0xffa7c4e1),
        .playhead = juce::Colour (0xffffc857),
        .recording = juce::Colour (0xffffad93),
        .success = juce::Colour (0xff5cd7a0),
        .warning = juce::Colour (0xfff2c14e),
        .danger = juce::Colour (0xffffadad),

        .funcTone = juce::Colour (0xffc9ab81),
        .funcTime = juce::Colour (0xffabbf7c),
        .funcLevel = juce::Colour (0xff80c4a0),
        .funcStereo = juce::Colour (0xff7fc7c7),
        .funcSpace = juce::Colour (0xff9792dc),
        .funcModulation = juce::Colour (0xffc88ad1),
        .funcPitch = juce::Colour (0xffd48ca4),

        .keyBlack = juce::Colour (0xff000000),
        .keyWhite = juce::Colour (0xfff2f4f7),

        .beatShade = juce::Colour (0xff0a0c0f),
        .barShade = juce::Colour (0xff131720),
    };
}

// clang-format on

} // namespace dew::tokens::colour

namespace dew::tokens::type
{

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monospaced (float height)
{
    return juce::Font (
        juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

} // namespace dew::tokens::type
