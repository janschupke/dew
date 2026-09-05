// Actions and automation shapes.
//
// One of the translation units holding dew's icon catalog, which was 682 lines
// in one file. The shared stroke helpers are IconStroke.h.
//
// These are the icons a MENU spends rather than a toolbar: the row's leading
// glyph in a context menu. They have to read at 14px beside a word, which is
// smaller than any toolbar button draws, so each one is a silhouette first and
// a picture second.

#include "ui/design/Icons.h"

#include <cmath>

#include "ui/design/icons/IconStroke.h"
#include "ui/design/Tokens.h"

namespace dew::icons
{

namespace
{

/** The two ends every automation shape runs between.

    Shared rather than written out three times, because the three differ ONLY in
    what happens between those ends - which is exactly the difference the menu
    is asking about. Anything else that varied would be noise on the answer.

    Drawn with no end points on them, deliberately. A dot at each end is what
    automation() wears and it is the right idea at 26px; at the 14px a menu row
    gives a glyph it is a two-pixel disc on a one-pixel line, which reads as a
    thickened end rather than as a point and costs the shape its silhouette.
*/
constexpr float startX = 0.16f;
constexpr float startY = 0.76f;
constexpr float endX = 0.84f;
constexpr float endY = 0.24f;

} // namespace

// --- actions -----------------------------------------------------------------

juce::Path open()
{
    // A frame with its top-right corner left out and an arrow going out through
    // the gap. Not a folder: nothing here browses a filesystem, and "open" in
    // dew means bringing a pattern the project already holds into view.
    juce::Path frame;
    frame.startNewSubPath (0.60f, 0.16f);
    frame.lineTo (0.16f, 0.16f);
    frame.lineTo (0.16f, 0.84f);
    frame.lineTo (0.84f, 0.84f);
    frame.lineTo (0.84f, 0.42f);

    auto p = strokeOf (frame, tokens::icon::regular);

    juce::Path shaft;
    shaft.startNewSubPath (0.44f, 0.56f);
    shaft.lineTo (0.84f, 0.16f);
    p.addPath (strokeOf (shaft, tokens::icon::regular));

    p.addTriangle (0.88f, 0.12f, 0.58f, 0.12f, 0.88f, 0.42f);
    return p;
}

juce::Path reset()
{
    // A ring with one head, turning back on itself. The loop icon is a ring
    // too, and the pair is told apart by the count: loop carries two heads
    // because it goes round again, reset carries one because it goes back.
    constexpr auto pi = juce::MathConstants<float>::pi;
    constexpr auto openEnd = 0.32f * pi;
    constexpr auto radius = 0.30f;

    juce::Path ring;
    ring.addCentredArc (0.5f, 0.5f, radius, radius, 0.0f, openEnd, 1.94f * pi, true);
    auto p = strokeOf (ring, tokens::icon::regular);

    // Placed from the same angle the arc opens at rather than from written-out
    // coordinates, so the head cannot drift off the end of the ring.
    const juce::Point<float> end { 0.5f + radius * std::sin (openEnd),
                                   0.5f - radius * std::cos (openEnd) };
    const juce::Point<float> back { -std::cos (openEnd), -std::sin (openEnd) };
    const juce::Point<float> across { back.y, -back.x };

    p.addTriangle (end + back * 0.15f, end + across * 0.11f, end - across * 0.11f);
    return p;
}

juce::Path palette()
{
    // A droplet. A paint pot's handle and an artist's palette's thumb hole are
    // both a couple of pixels at the size a menu row draws this, and an icon
    // whose distinguishing feature disappears is a filled blob.
    juce::Path drop;
    drop.startNewSubPath (0.5f, 0.11f);
    drop.quadraticTo (0.82f, 0.47f, 0.82f, 0.62f);
    drop.quadraticTo (0.82f, 0.87f, 0.5f, 0.87f);
    drop.quadraticTo (0.18f, 0.87f, 0.18f, 0.62f);
    drop.quadraticTo (0.18f, 0.47f, 0.5f, 0.11f);
    drop.closeSubPath();
    return drop;
}

juce::Path pattern()
{
    // A rhythm: four slots with the third one empty. Four FULL blocks would be
    // a row of blocks and could be anything; the hole is what makes it a
    // pattern rather than a ruler.
    juce::Path p;
    const bool filled[] = { true, true, false, true };

    for (int i = 0; i < 4; ++i)
        if (filled[i])
            p.addRoundedRectangle (0.08f + (float) i * 0.22f, 0.36f, 0.18f, 0.28f, 0.05f);

    return p;
}

// --- automation shapes -------------------------------------------------------

juce::Path shapeLine()
{
    juce::Path segment;
    segment.startNewSubPath (startX, startY);
    segment.lineTo (endX, endY);
    return strokeOf (segment, tokens::icon::regular);
}

juce::Path shapeCurve()
{
    // Both control points on the MIDLINE, which makes an S that leaves flat and
    // arrives flat. Pulled towards the ends instead - which is the shallower
    // curve an eased segment actually draws - it is a line with a kink in it,
    // and beside shapeLine in the same menu the two were one glyph twice.
    juce::Path segment;
    segment.startNewSubPath (startX, startY);
    segment.cubicTo (0.5f, startY, 0.5f, endY, endX, endY);
    return strokeOf (segment, tokens::icon::regular);
}

juce::Path shapeStep()
{
    juce::Path segment;
    segment.startNewSubPath (startX, startY);
    segment.lineTo (0.5f, startY);
    segment.lineTo (0.5f, endY);
    segment.lineTo (endX, endY);
    return strokeOf (segment, tokens::icon::regular);
}

} // namespace dew::icons
