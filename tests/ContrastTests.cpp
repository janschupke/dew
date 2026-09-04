#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

using namespace dew;
using namespace dew::tokens;

/*  What the palette has to be legible against.

    dew's colours are named for their ROLE, which is what lets a theme change be
    an edit to one file - but a role says nothing about whether the pair is
    readable, and five of them were not. The worst was the hover-help line: the
    app's only always-on explanation of the control under the pointer, drawn in
    the palette's least readable colour.

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

struct Pair
{
    const char* foreground;
    juce::Colour on;
    const char* background;
    juce::Colour against;
};

/** Every surface a panel or a grid is painted with. */
const std::vector<std::pair<const char*, juce::Colour>> surfaces {
    { "wellDeep", colour::wellDeep },           { "well", colour::well },
    { "background", colour::background },       { "surface", colour::surface },
    { "surfaceRaised", colour::surfaceRaised }, { "surfaceHover", colour::surfaceHover },
};

/** The two a control is painted with, where "disabled" is a thing to be. */
const std::vector<std::pair<const char*, juce::Colour>> controlSurfaces {
    { "surfaceRaised", colour::surfaceRaised },
    { "surfaceHover", colour::surfaceHover },
};

/** The four a document or a panel is painted with - where text is CONTENT. */
const std::vector<std::pair<const char*, juce::Colour>> contentSurfaces {
    { "wellDeep", colour::wellDeep },
    { "well", colour::well },
    { "background", colour::background },
    { "surface", colour::surface },
};

void checkAll (const std::vector<std::pair<const char*, juce::Colour>>& grounds, const char* name,
               juce::Colour c, float required, juce::StringArray& failures)
{
    for (const auto& [groundName, ground] : grounds)
        if (const auto ratio = contrastRatio (c, ground); ratio < required)
            failures.add (juce::String (name) + " on " + groundName + "  " + juce::String (ratio, 2)
                          + ":1, needs " + juce::String (required, 1));
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
}

TEST_CASE ("every text role is readable on the surfaces it is drawn on", "[design][contrast]")
{
    juce::StringArray failures;

    // 4.5:1 - WCAG 1.4.3 for text below 18pt, which is all of dew's: the type
    // scale tops out at 20 and almost every label lands on 11 or 12.
    constexpr auto bodyText = 4.5f;

    checkAll (surfaces, "textPrimary", colour::textPrimary, bodyText, failures);
    checkAll (surfaces, "textSecondary", colour::textSecondary, bodyText, failures);
    checkAll (surfaces, "playhead", colour::playhead, bodyText, failures);
    checkAll (surfaces, "success", colour::success, bodyText, failures);
    checkAll (surfaces, "warning", colour::warning, bodyText, failures);
    checkAll (surfaces, "danger", colour::danger, bodyText, failures);
    checkAll (surfaces, "recording", colour::recording, bodyText, failures);

    // The accent is text on a PANEL - a selected slot's name, a heading - and
    // never on a hovered control, whose own text is textPrimary or textOnAccent.
    // Listed against the grounds it actually lands on rather than against all
    // six, because a gate asserting a pair that is never painted is fiction.
    checkAll (contentSurfaces, "accent", colour::accent, bodyText, failures);
    checkAll ({ { "surfaceRaised", colour::surfaceRaised } }, "accent", colour::accent, bodyText,
              failures);

    // textDisabled does two jobs. On a panel it is CONTENT that happens to be
    // quiet - a comment in the score editor, a bar number past the end of the
    // song, the hover-help line - and content has to be readable.
    checkAll (contentSurfaces, "textDisabled", colour::textDisabled, bodyText, failures);

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("a disabled control is quiet but not invisible", "[design][contrast]")
{
    juce::StringArray failures;

    // On a control surface, textDisabled means "you cannot use this", which
    // WCAG exempts from 1.4.3 outright. 3:1 is dew's own floor rather than a
    // requirement: a disabled button still has to read as a button, which is
    // the distinction emphasis::disabled exists to make.
    checkAll (controlSurfaces, "textDisabled", colour::textDisabled, 3.0f, failures);

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("a control's edge and its focus ring are visible", "[design][contrast]")
{
    juce::StringArray failures;

    // 3:1 - WCAG 1.4.11, for the boundary that identifies a control.
    //
    // It carries the whole job here: surfaceRaised, which is what a button is
    // filled with, sits at 1.14 to 1.42 against every ground it is placed on,
    // so a dew control is its outline. At the old 0xff454c57 the outline was
    // 1.55, and between the two of them nothing said where the button was.
    constexpr auto nonText = 3.0f;

    checkAll (surfaces, "outline", colour::outline, nonText, failures);
    checkAll (surfaces, "accent (focus ring)", colour::accent, nonText, failures);

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("text on a filled control is readable on every fill", "[design][contrast]")
{
    juce::StringArray failures;
    constexpr auto bodyText = 4.5f;

    // textOnAccent is the label on anything the accent-family fills: a primary
    // button, a toggled icon button, a selected menu row, a clip.
    const std::vector<std::pair<const char*, juce::Colour>> fills {
        { "accent", colour::accent },     { "accentMuted", colour::accentMuted },
        { "playhead", colour::playhead }, { "success", colour::success },
        { "warning", colour::warning },   { "danger", colour::danger },
        { "keyWhite", colour::keyWhite },
    };

    checkAll (fills, "textOnAccent", colour::textOnAccent, bodyText, failures);

    // And on every channel colour, because a clip is filled with one and its
    // name is drawn in textOnAccent regardless of which it got.
    for (int i = 0; i < 8; ++i)
    {
        const auto c = colour::channelColour (i);

        if (const auto ratio = contrastRatio (colour::textOnAccent, c); ratio < bodyText)
            failures.add ("textOnAccent on channelRamp[" + juce::String (i) + "]  "
                          + juce::String (ratio, 2) + ":1");
    }

    INFO (failures.joinIntoString ("\n"));
    CHECK (failures.isEmpty());
}

TEST_CASE ("a piano key reads against the other kind", "[design][contrast]")
{
    // The one pair in the palette whose whole job is to be told apart.
    CHECK (contrastRatio (colour::keyWhite, colour::keyBlack) >= 3.0f);
}
