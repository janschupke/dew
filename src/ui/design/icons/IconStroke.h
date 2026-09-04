#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

namespace dew::icons
{

/** How every icon in dew is built.

    Strokes are drawn as OUTLINES so an icon is one filled path: it scales and
    recolours as a unit, and a caller never has to know a stroke width. Shared
    by the three files the catalog is split across - a second idea of what a
    stroke is would be visible immediately, as one icon heavier than the rest.
*/

namespace
{

/** Strokes are drawn as outlines so an icon is one filled path - it scales and
    recolours as a unit, and callers never have to know a stroke width.
*/
inline juce::Path strokedLine (float x1, float y1, float x2, float y2,
                               float thickness = tokens::icon::regular)
{
    juce::Path line;
    line.startNewSubPath (x1, y1);
    line.lineTo (x2, y2);

    juce::Path stroked;
    juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath (stroked, line);
    return stroked;
}

inline juce::Path strokeOf (const juce::Path& source, float thickness = tokens::icon::regular)
{
    juce::Path stroked;
    juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath (stroked, source);
    return stroked;
}

} // namespace

} // namespace dew::icons
