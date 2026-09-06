#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Theme.h"
#include "ui/design/Tokens.h"

using namespace dew;
using namespace dew::tokens;

/*  What a palette has to be legible against.

    dew's colours are named for their ROLE, which is what lets a theme be one
    assignment - but a role says nothing about whether the pair is readable, and
    five of them were not. The worst was the hover-help line: the app's only
    always-on explanation of the control under the pointer, drawn in the
    palette's least readable colour at 2.6:1.

    Every check below runs over EVERY palette, at that palette's own thresholds.
    A theme is not a set of colours somebody liked; it is a set of colours that
    clears a bar, and adding one means clearing it too.

    The pairs are written out rather than derived, so a new token has to be
    added to a table deliberately and a changed one has to be argued with.
*/

namespace
{

/** WCAG 2.1's relative luminance: sRGB, gamma-expanded, Rec.709 weights. */
float relativeLuminance (juce::Colour c)
{
    const auto channel = [] (float v)
    { return v <= 0.04045f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };

    return 0.2126f * channel (c.getFloatRed()) + 0.7152f * channel (c.getFloatGreen())
           + 0.0722f * channel (c.getFloatBlue());
}

/** 1.0 for two identical colours, 21.0 for black on white. */
float contrastRatio (juce::Colour a, juce::Colour b)
{
    const auto la = relativeLuminance (a);
    const auto lb = relativeLuminance (b);

    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
}

/** A palette and what it promises.

    Dark is held to WCAG AA - 4.5:1 for text, 3:1 for a boundary. High contrast
    is held to AAA, one grade up, which is the whole reason it exists.
*/
struct Theme
{
    const char* name;
    colour::Palette palette;
    float text;
    float nonText;
};

std::vector<Theme> themes()
{
    return { { "dark", colour::darkPalette(), 4.5f, 3.0f },
             { "high contrast", colour::highContrastPalette(), 7.0f, 4.5f } };
}

using Ground = std::pair<const char*, juce::Colour>;

/** Every surface a panel or a grid is painted with. */
std::vector<Ground> surfaces (const colour::Palette& p)
{
    return { { "wellDeep", p.wellDeep },           { "well", p.well },
             { "background", p.background },       { "surface", p.surface },
             { "surfaceRaised", p.surfaceRaised }, { "surfaceHover", p.surfaceHover } };
}

/** The two a control is painted with, where "disabled" is a thing to be. */
std::vector<Ground> controlSurfaces (const colour::Palette& p)
{
    return { { "surfaceRaised", p.surfaceRaised }, { "surfaceHover", p.surfaceHover } };
}

/** The four a document or a panel is painted with - where text is CONTENT. */
std::vector<Ground> contentSurfaces (const colour::Palette& p)
{
    return { { "wellDeep", p.wellDeep },
             { "well", p.well },
             { "background", p.background },
             { "surface", p.surface } };
}

void checkAll (const std::vector<Ground>& grounds, const char* name, juce::Colour c, float required,
               const char* theme, juce::StringArray& failures)
{
    for (const auto& [groundName, ground] : grounds)
        if (const auto ratio = contrastRatio (c, ground); ratio < required)
            failures.add (juce::String (theme) + ": " + name + " on " + groundName + "  "
                          + juce::String (ratio, 2) + ":1, needs " + juce::String (required, 1));
}

} // namespace

TEST_CASE ("the contrast helper agrees with the specification", "[design][contrast]")
{
    // The control case. A gate computing the wrong number passes for the wrong
    // reason, and these three are the values WCAG's own worked examples give.
    CHECK (contrastRatio (juce::Colours::black, juce::Colours::white)
           == Catch::Approx (21.0f).margin (0.01f));
    CHECK (contrastRatio (juce::Colours::white, juce::Colours::white)
           == Catch::Approx (1.0f).margin (0.01f));
    CHECK (contrastRatio (juce::Colour (0xff777777), juce::Colours::white)
           == Catch::Approx (4.48f).margin (0.02f));

    // And a control case over the themes themselves: a loop over an empty list
    // is not a gate, and a second theme that was never added would pass every
    // case below in silence.
    REQUIRE (themes().size() == 2);
}

TEST_CASE ("every text role is readable on the surfaces it is drawn on", "[design][contrast]")
{
    juce::StringArray failures;

    for (const auto& [name, p, text, nonText] : themes())
    {
        juce::ignoreUnused (nonText);

        for (const auto& [role, c] : std::vector<Ground> { { "textPrimary", p.textPrimary },
                                                           { "textSecondary", p.textSecondary },
                                                           { "playhead", p.playhead },
                                                           { "success", p.success },
                                                           { "warning", p.warning },
                                                           { "danger", p.danger } })
            checkAll (surfaces (p), role, c, text, name, failures);

        // recording is NOT in that list, and its absence is the rule rather
        // than an omission: it is a FILL-ONLY colour. Hue 0 at value 1 clears
        // 4.5:1 on surfaceHover only up to the saturation danger already sits
        // at, so a deeper red cannot be drawn WITH on these grounds - and a
        // record control rests in danger for exactly that reason. What it has
        // to clear is the fill rule below. Listing it here as well would be a
        // gate asserting a pair nothing paints, which is what the accent's own
        // note two lines down refuses.

        // The accent is text on a PANEL - a selected slot's name, a heading -
        // and never on a hovered control, whose own text is textPrimary or
        // textOnAccent. Listed against the grounds it actually lands on rather
        // than against all six, because a gate asserting a pair that is never
        // painted is fiction.
        checkAll (contentSurfaces (p), "accent", p.accent, text, name, failures);
        checkAll ({ { "surfaceRaised", p.surfaceRaised } }, "accent", p.accent, text, name,
                  failures);

        // textDisabled does two jobs. On a panel it is CONTENT that happens to
        // be quiet - a comment in the score editor, a bar number past the end of
        // the song, the hover-help line - and content has to be readable.
        checkAll (contentSurfaces (p), "textDisabled", p.textDisabled, text, name, failures);
    }

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("a disabled control is quiet but not invisible", "[design][contrast]")
{
    juce::StringArray failures;

    // On a control surface, textDisabled means "you cannot use this", which
    // WCAG exempts from 1.4.3 outright. This is dew's own floor rather than a
    // requirement: a disabled button still has to read as a button, which is
    // the distinction emphasis::disabled exists to make.
    for (const auto& [name, p, text, nonText] : themes())
    {
        juce::ignoreUnused (text);
        checkAll (controlSurfaces (p), "textDisabled", p.textDisabled, nonText, name, failures);
    }

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("a control's edge and its focus ring are visible", "[design][contrast]")
{
    juce::StringArray failures;

    // WCAG 1.4.11, for the boundary that identifies a control.
    //
    // It carries the whole job here: surfaceRaised, which is what a button is
    // filled with, sits close to every ground it is placed on, so a dew control
    // is its outline. At the original 0xff454c57 the outline was 1.55:1, and
    // between the two of them nothing said where the button was.
    for (const auto& [name, p, text, nonText] : themes())
    {
        juce::ignoreUnused (text);

        checkAll (surfaces (p), "outline", p.outline, nonText, name, failures);
        checkAll (surfaces (p), "accent (focus ring)", p.accent, nonText, name, failures);

        // The function colours are knob arcs and automation curves - things you
        // find rather than read - so they are held to the non-text bar.
        for (const auto& [role, c] : std::vector<Ground> { { "funcTone", p.funcTone },
                                                           { "funcTime", p.funcTime },
                                                           { "funcLevel", p.funcLevel },
                                                           { "funcStereo", p.funcStereo },
                                                           { "funcSpace", p.funcSpace },
                                                           { "funcModulation", p.funcModulation },
                                                           { "funcPitch", p.funcPitch } })
            checkAll (surfaces (p), role, c, nonText, name, failures);
    }

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("text on a filled control is readable on every fill", "[design][contrast]")
{
    juce::StringArray failures;

    for (const auto& [name, p, text, nonText] : themes())
    {
        juce::ignoreUnused (nonText);

        // textOnAccent is the label on anything the accent family fills: a
        // primary button, a toggled icon button, a selected menu row, a clip.
        checkAll ({ { "accent", p.accent },
                    { "accentMuted", p.accentMuted },
                    { "playhead", p.playhead },
                    { "success", p.success },
                    { "warning", p.warning },
                    { "danger", p.danger },
                    // The armed record button and the armed "R" on a channel
                    // row. This is the ONLY rule recording answers to, and the
                    // reason it is allowed to be the deep red danger cannot be.
                    { "recording", p.recording },
                    { "keyWhite", p.keyWhite } },
                  "textOnAccent", p.textOnAccent, text, name, failures);

        // The channel ramp is the exception, and it is a real one rather than a
        // concession. Those eight colours are DOCUMENT DATA - entityColour
        // writes them into every .dew file and the picker offers them - so no
        // theme may repaint them, and the label drawn on one can only be held
        // to AA. Raising this to AAA would mean changing what a saved project
        // means.
        for (int i = 0; i < 8; ++i)
        {
            const auto c = colour::channelColour (i);

            if (const auto ratio = contrastRatio (p.textOnAccent, c); ratio < 4.5f)
                failures.add (juce::String (name) + ": textOnAccent on channelRamp["
                              + juce::String (i) + "]  " + juce::String (ratio, 2) + ":1");
        }
    }

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("a piano key reads against the other kind", "[design][contrast]")
{
    juce::StringArray failures;

    // The one pair in the palette whose whole job is to be told apart.
    for (const auto& [name, p, text, nonText] : themes())
    {
        juce::ignoreUnused (text);

        if (const auto ratio = contrastRatio (p.keyWhite, p.keyBlack); ratio < nonText)
            failures.add (juce::String (name) + ": keyWhite on keyBlack "
                          + juce::String (ratio, 2));
    }

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}
