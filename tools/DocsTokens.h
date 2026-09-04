#pragma once

#include <string>

#include <juce_graphics/juce_graphics.h>

#include "ui/design/Tokens.h"
#include "DocsJson.h"

namespace dew::docs
{

/** A colour as CSS.

    Not juce::Colour::toString(), which writes ARGB in JUCE's order and is
    JUCE's format to change. The site writes #rrggbb, which is CSS's.

    Formatting a colour at RUNTIME is not what the hex gate refuses. That gate
    scans for the literal `juce::Colour (0x` - a colour no theme can change and
    no reader can name - and there is none here: every value is read out of
    darkPalette().
*/
inline std::string cssColour (juce::Colour c)
{
    const auto pair = [] (juce::uint8 v)
    { return juce::String::toHexString ((int) v).paddedLeft ('0', 2).toLowerCase().toStdString(); };

    auto text = "#" + pair (c.getRed()) + pair (c.getGreen()) + pair (c.getBlue());

    // Every role in darkPalette() is opaque today. If one stops being, the site
    // gets eight digits rather than a silently wrong colour.
    if (c.getAlpha() != 255)
        text += pair (c.getAlpha());

    return text;
}

/** The design system as JSON, from the DARK palette.

    darkPalette() directly rather than colour::active, so the emitter does not
    depend on whether anything called theme::applyPalette first. The site is
    dark, one palette; the high-contrast one is the application's and is not
    emitted.

    tokens::size is deliberately absent. The gutters, strip heights and lane
    ranges are the application's internal geometry, and shipping them would
    invite a website to restate an app dimension in CSS - which is exactly the
    second vocabulary Tokens.h exists to end.
*/
inline std::string tokensJson()
{
    const auto palette = tokens::colour::darkPalette();

    JsonWriter json;
    json.beginObject();

    json.key ("palette");
    json.value ("dark");

    json.key ("colour");
    json.beginObject();

    // clang-format off
    const struct { const char* name; juce::Colour value; } roles[] {
        { "wellDeep",       palette.wellDeep },
        { "well",           palette.well },
        { "background",     palette.background },
        { "surface",        palette.surface },
        { "surfaceRaised",  palette.surfaceRaised },
        { "surfaceHover",   palette.surfaceHover },
        { "divider",        palette.divider },
        { "dividerStrong",  palette.dividerStrong },
        { "outline",        palette.outline },
        { "textPrimary",    palette.textPrimary },
        { "textSecondary",  palette.textSecondary },
        { "textDisabled",   palette.textDisabled },
        { "textOnAccent",   palette.textOnAccent },
        { "accent",         palette.accent },
        { "accentMuted",    palette.accentMuted },
        { "playhead",       palette.playhead },
        { "recording",      palette.recording },
        { "success",        palette.success },
        { "warning",        palette.warning },
        { "danger",         palette.danger },
        { "funcTone",       palette.funcTone },
        { "funcTime",       palette.funcTime },
        { "funcLevel",      palette.funcLevel },
        { "funcStereo",     palette.funcStereo },
        { "funcSpace",      palette.funcSpace },
        { "funcModulation", palette.funcModulation },
        { "funcPitch",      palette.funcPitch },
        { "keyBlack",       palette.keyBlack },
        { "keyWhite",       palette.keyWhite },
        { "beatShade",      palette.beatShade },
        { "barShade",       palette.barShade },
    };

    // clang-format on
    for (const auto& role : roles)
    {
        json.key (role.name);
        json.value (cssColour (role.value));
    }

    json.endObject();

    // Not themeable: document data, written into every .dew file and restated
    // in dew_model. A website that recoloured these would disagree with every
    // saved project.
    json.key ("channelRamp");
    json.beginArray();

    for (const auto& c : tokens::colour::channelRamp)
        json.value (cssColour (c));

    json.endArray();

    /*  A lift is a juce::Colour::brighter() call, and brighter() is
        255 - (1/(1+amount)) * (255 - channel) per sRGB channel, TRUNCATED to a
        uint8. CSS has no such function: color-mix rounds where this truncates,
        and "6% lighter" is a different colour again.

        So the composed results are emitted, computed here by the same call the
        application paints with. Nothing reimplements the formula anywhere.

        Worth knowing while reading these: a hovered `normal` button is
        surfaceRaised lifted by controlLift, which is NOT colour::surfaceHover.
        They are different colours doing different jobs, and reaching for the
        token whose name says "hover" would give a web control the darker one.
    */
    json.key ("lift");
    json.beginObject();

    // clang-format off
    const struct { const char* name; juce::Colour base; float amount; } lifts[] {
        { "surfaceLifted",      palette.surface,       tokens::emphasis::surfaceLift },
        { "surfaceRaisedHover", palette.surfaceRaised, tokens::emphasis::controlLift },
        { "surfaceRaisedPress", palette.surfaceRaised, tokens::emphasis::pressLift },
        { "accentMutedHover",   palette.accentMuted,   tokens::emphasis::controlLift },
        { "accentMutedPress",   palette.accentMuted,   tokens::emphasis::pressLift },
        { "accentHover",        palette.accent,        tokens::emphasis::controlLift },
        { "accentPress",        palette.accent,        tokens::emphasis::pressLift },
    };

    // clang-format on
    for (const auto& lift : lifts)
    {
        json.key (lift.name);
        json.value (cssColour (lift.base.brighter (lift.amount)));
    }

    json.endObject();

    json.key ("emphasis");
    json.beginObject();

    // clang-format off
    const struct { const char* name; float value; } alphas[] {
        { "tint",        tokens::emphasis::tint },
        { "wash",        tokens::emphasis::wash },
        { "hatch",       tokens::emphasis::hatch },
        { "subdued",     tokens::emphasis::subdued },
        { "dimmed",      tokens::emphasis::dimmed },
        { "strong",      tokens::emphasis::strong },
        { "surfaceLift", tokens::emphasis::surfaceLift },
        { "controlLift", tokens::emphasis::controlLift },
        { "pressLift",   tokens::emphasis::pressLift },
        { "edgeLift",    tokens::emphasis::edgeLift },
    };

    const struct { const char* name; int value; } spaces[] {
        { "xxs", tokens::space::xxs }, { "xs", tokens::space::xs },
        { "sm",  tokens::space::sm },  { "md", tokens::space::md },
        { "lg",  tokens::space::lg },  { "xl", tokens::space::xl },
        { "xxl", tokens::space::xxl },
    };

    const struct { const char* name; float value; } radii[] {
        { "xs", tokens::radius::xs }, { "sm", tokens::radius::sm },
        { "md", tokens::radius::md }, { "lg", tokens::radius::lg },
    };

    const struct { const char* name; float value; } strokes[] {
        { "whisper", tokens::stroke::whisper }, { "hairline", tokens::stroke::hairline },
        { "regular", tokens::stroke::regular }, { "bold",     tokens::stroke::bold },
    };

    const struct { const char* name; float value; } types[] {
        { "caption",   tokens::type::caption },   { "small",     tokens::type::small },
        { "body",      tokens::type::body },      { "title",     tokens::type::title },
        { "display",   tokens::type::display },   { "codeSmall", tokens::type::codeSmall },
        { "codeBody",  tokens::type::codeBody },  { "codeLarge", tokens::type::codeLarge },
        { "codeHuge",  tokens::type::codeHuge },
    };

    const struct { const char* name; int value; } motions[] {
        { "selectMs",    tokens::motion::selectMs },
        { "quickMs",     tokens::motion::quickMs },
        { "valueMs",     tokens::motion::valueMs },
        { "popupMs",     tokens::motion::popupMs },
        { "panelMs",     tokens::motion::panelMs },
        { "popupRisePx", tokens::motion::popupRisePx },
    };

    // clang-format on
    for (const auto& alpha : alphas)
    {
        json.key (alpha.name);
        json.value (alpha.value);
    }

    json.endObject();

    const auto writeInts = [&json] (const char* name, auto& table)
    {
        json.key (name);
        json.beginObject();

        for (const auto& entry : table)
        {
            json.key (entry.name);
            json.value (entry.value);
        }

        json.endObject();
    };

    writeInts ("space", spaces);
    writeInts ("radius", radii);
    writeInts ("stroke", strokes);
    writeInts ("type", types);
    writeInts ("motion", motions);

    json.endObject();

    return json.str();
}

} // namespace dew::docs
