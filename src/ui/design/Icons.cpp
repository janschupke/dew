#include "ui/design/Icons.h"

#include "ui/design/Tokens.h"

namespace dew::icons
{

namespace
{

/** Strokes are drawn as outlines so an icon is one filled path - it scales and
    recolours as a unit, and callers never have to know a stroke width.
*/
juce::Path strokedLine (float x1, float y1, float x2, float y2, float thickness = tokens::icon::regular)
{
    juce::Path line;
    line.startNewSubPath (x1, y1);
    line.lineTo (x2, y2);

    juce::Path stroked;
    juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (stroked, line);
    return stroked;
}

juce::Path strokeOf (const juce::Path& source, float thickness = tokens::icon::regular)
{
    juce::Path stroked;
    juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (stroked, source);
    return stroked;
}

} // namespace

// --- transport ---------------------------------------------------------------

juce::Path play()
{
    juce::Path p;
    p.addTriangle (0.2f, 0.1f, 0.2f, 0.9f, 0.85f, 0.5f);
    return p;
}

juce::Path pause()
{
    juce::Path p;
    p.addRoundedRectangle (0.22f, 0.12f, 0.18f, 0.76f, 0.03f);
    p.addRoundedRectangle (0.60f, 0.12f, 0.18f, 0.76f, 0.03f);
    return p;
}

juce::Path stop()
{
    juce::Path p;
    p.addRoundedRectangle (0.2f, 0.2f, 0.6f, 0.6f, 0.06f);
    return p;
}

juce::Path rewind()
{
    juce::Path p;
    p.addRoundedRectangle (0.15f, 0.15f, 0.12f, 0.7f, 0.03f);
    p.addTriangle (0.85f, 0.15f, 0.85f, 0.85f, 0.32f, 0.5f);
    return p;
}

juce::Path record()
{
    juce::Path p;
    p.addEllipse (0.22f, 0.22f, 0.56f, 0.56f);
    return p;
}

juce::Path loop()
{
    juce::Path arc;
    arc.addCentredArc (0.5f, 0.5f, 0.32f, 0.32f, 0.0f,
                       juce::MathConstants<float>::pi * 0.35f,
                       juce::MathConstants<float>::pi * 1.85f, true);

    auto p = strokeOf (arc, tokens::icon::regular);
    p.addTriangle (0.62f, 0.06f, 0.62f, 0.34f, 0.90f, 0.20f);
    return p;
}

// --- editing -----------------------------------------------------------------

juce::Path plus()
{
    juce::Path p;
    p.addRoundedRectangle (0.44f, 0.16f, 0.12f, 0.68f, 0.04f);
    p.addRoundedRectangle (0.16f, 0.44f, 0.68f, 0.12f, 0.04f);
    return p;
}

juce::Path minus()
{
    juce::Path p;
    p.addRoundedRectangle (0.16f, 0.44f, 0.68f, 0.12f, 0.04f);
    return p;
}

juce::Path trash()
{
    juce::Path p;
    p.addRoundedRectangle (0.24f, 0.26f, 0.52f, 0.60f, 0.06f);
    p.addRoundedRectangle (0.16f, 0.16f, 0.68f, 0.10f, 0.04f);
    p.addRoundedRectangle (0.40f, 0.08f, 0.20f, 0.10f, 0.04f);
    return p;
}

juce::Path duplicate()
{
    juce::Path p;
    p.addRoundedRectangle (0.12f, 0.12f, 0.52f, 0.52f, 0.06f);
    p.addRoundedRectangle (0.36f, 0.36f, 0.52f, 0.52f, 0.06f);
    return p;
}

juce::Path pencil()
{
    juce::Path body;
    body.startNewSubPath (0.18f, 0.82f);
    body.lineTo (0.28f, 0.60f);
    body.lineTo (0.72f, 0.16f);
    body.lineTo (0.86f, 0.30f);
    body.lineTo (0.42f, 0.74f);
    body.closeSubPath();
    return body;
}

juce::Path magnet()
{
    // A horseshoe opening downwards with blunt poles. Deliberately not an arc
    // with two thin legs, which is the headphone silhouette used for solo.
    juce::Path outer;
    outer.addCentredArc (0.5f, 0.52f, 0.34f, 0.34f, 0.0f,
                         -juce::MathConstants<float>::halfPi,
                         juce::MathConstants<float>::halfPi, true);

    auto p = strokeOf (outer, tokens::icon::ring);

    // Square off the poles so it reads as a magnet rather than a hook.
    p.addRectangle (0.11f, 0.52f, 0.26f, 0.30f);
    p.addRectangle (0.63f, 0.52f, 0.26f, 0.30f);
    return p;
}

juce::Path pointer()
{
    juce::Path p;
    p.startNewSubPath (0.24f, 0.12f);
    p.lineTo (0.24f, 0.80f);
    p.lineTo (0.42f, 0.64f);
    p.lineTo (0.54f, 0.88f);
    p.lineTo (0.66f, 0.82f);
    p.lineTo (0.54f, 0.60f);
    p.lineTo (0.76f, 0.56f);
    p.closeSubPath();
    return p;
}

juce::Path eraser()
{
    // A tilted block with a separated tip, so the two halves stay legible.
    juce::Path body;
    body.startNewSubPath (0.16f, 0.68f);
    body.lineTo (0.50f, 0.20f);
    body.lineTo (0.86f, 0.44f);
    body.lineTo (0.52f, 0.84f);
    body.closeSubPath();

    juce::Path p (body);
    p.addPath (strokedLine (0.34f, 0.44f, 0.68f, 0.66f, tokens::icon::hair));
    return p;
}

juce::Path scissors()
{
    // Blades in the top half, rings in the bottom. The rings have to be big
    // enough to survive a 24px button - drawn smaller they close up into two
    // dots and the whole thing reads as a cross.
    juce::Path p;
    p.addPath (strokedLine (0.26f, 0.10f, 0.62f, 0.56f, tokens::icon::regular));
    p.addPath (strokedLine (0.74f, 0.10f, 0.38f, 0.56f, tokens::icon::regular));

    juce::Path rings;
    rings.addEllipse (0.12f, 0.58f, 0.30f, 0.30f);
    rings.addEllipse (0.58f, 0.58f, 0.30f, 0.30f);
    p.addPath (strokeOf (rings, tokens::icon::regular));

    return p;
}

juce::Path dice()
{
    // A five face, not a diagonal three: the corner pips are what make this a
    // die rather than a framed picture, and a diagonal row merges with the
    // border once the icon is small.
    juce::Path p;
    p.addPath (strokeOf ([]
    {
        juce::Path body;
        body.addRoundedRectangle (0.12f, 0.12f, 0.76f, 0.76f, 0.16f);
        return body;
    }(), 0.09f));

    const auto pip = [&p] (float x, float y) { p.addEllipse (x - 0.075f, y - 0.075f, 0.15f, 0.15f); };

    pip (0.32f, 0.32f);
    pip (0.68f, 0.32f);
    pip (0.50f, 0.50f);
    pip (0.32f, 0.68f);
    pip (0.68f, 0.68f);

    return p;
}

juce::Path quantize()
{
    // A note sitting exactly inside a grid cell. Deliberately NOT magnet(),
    // which is the obvious "snap" glyph and which collapses into the headphone
    // silhouette that already means solo once it is button-sized.
    juce::Path p;

    for (const auto x : { 0.14f, 0.5f, 0.86f })
        p.addPath (strokedLine (x, 0.10f, x, 0.90f, tokens::icon::hair));

    // Centred ON the middle line, not butted against one of the outer two:
    // touching a line merges the two shapes into a letter H at button size.
    juce::Path block;
    block.addRoundedRectangle (0.34f, 0.38f, 0.32f, 0.24f, 0.06f);
    p.addPath (block);

    return p;
}

// --- state -------------------------------------------------------------------

juce::Path mute()
{
    juce::Path p;
    p.addRectangle (0.12f, 0.38f, 0.16f, 0.24f);
    p.startNewSubPath (0.28f, 0.38f);
    p.lineTo (0.52f, 0.16f);
    p.lineTo (0.52f, 0.84f);
    p.lineTo (0.28f, 0.62f);
    p.closeSubPath();

    p.addPath (strokedLine (0.62f, 0.36f, 0.88f, 0.64f, tokens::icon::regular));
    p.addPath (strokedLine (0.88f, 0.36f, 0.62f, 0.64f, tokens::icon::regular));
    return p;
}

juce::Path solo()
{
    juce::Path band;
    band.addCentredArc (0.5f, 0.52f, 0.32f, 0.32f, 0.0f,
                        -juce::MathConstants<float>::halfPi * 1.6f,
                        juce::MathConstants<float>::halfPi * 1.6f, true);

    auto p = strokeOf (band, tokens::icon::regular);
    p.addRoundedRectangle (0.14f, 0.52f, 0.16f, 0.32f, 0.06f);
    p.addRoundedRectangle (0.70f, 0.52f, 0.16f, 0.32f, 0.06f);
    return p;
}

juce::Path power()
{
    juce::Path arc;
    arc.addCentredArc (0.5f, 0.55f, 0.3f, 0.3f, 0.0f,
                       juce::MathConstants<float>::pi * 0.25f,
                       juce::MathConstants<float>::pi * 1.75f, true);

    auto p = strokeOf (arc, tokens::icon::regular);
    p.addPath (strokedLine (0.5f, 0.1f, 0.5f, 0.45f, tokens::icon::regular));
    return p;
}

juce::Path lock()
{
    juce::Path shackle;
    shackle.addCentredArc (0.5f, 0.42f, 0.2f, 0.22f, 0.0f,
                           -juce::MathConstants<float>::halfPi,
                           juce::MathConstants<float>::halfPi, true);

    auto p = strokeOf (shackle, tokens::icon::regular);
    p.addRoundedRectangle (0.22f, 0.44f, 0.56f, 0.42f, 0.06f);
    return p;
}

juce::Path check()
{
    juce::Path line;
    line.startNewSubPath (0.22f, 0.52f);
    line.lineTo (0.42f, 0.72f);
    line.lineTo (0.78f, 0.28f);
    return strokeOf (line, tokens::icon::bold);
}

juce::Path chevronUp()
{
    juce::Path line;
    line.startNewSubPath (0.24f, 0.62f);
    line.lineTo (0.5f, 0.36f);
    line.lineTo (0.76f, 0.62f);
    return strokeOf (line, tokens::icon::bold);
}

juce::Path chevronDown()
{
    juce::Path line;
    line.startNewSubPath (0.24f, 0.38f);
    line.lineTo (0.5f, 0.64f);
    line.lineTo (0.76f, 0.38f);
    return strokeOf (line, tokens::icon::bold);
}

juce::Path chevronLeft()
{
    juce::Path line;
    line.startNewSubPath (0.62f, 0.24f);
    line.lineTo (0.36f, 0.5f);
    line.lineTo (0.62f, 0.76f);
    return strokeOf (line, tokens::icon::bold);
}

juce::Path chevronRight()
{
    juce::Path line;
    line.startNewSubPath (0.38f, 0.24f);
    line.lineTo (0.64f, 0.5f);
    line.lineTo (0.38f, 0.76f);
    return strokeOf (line, tokens::icon::bold);
}

juce::Path grip()
{
    juce::Path p;

    for (int row = 0; row < 3; ++row)
    {
        const auto y = 0.26f + (float) row * 0.24f;
        p.addEllipse (0.32f, y, 0.1f, 0.1f);
        p.addEllipse (0.58f, y, 0.1f, 0.1f);
    }

    return p;
}

// --- waveforms ---------------------------------------------------------------

juce::Path waveSine()
{
    juce::Path wave;
    wave.startNewSubPath (0.08f, 0.5f);

    for (int i = 1; i <= 32; ++i)
    {
        const auto t = (float) i / 32.0f;
        wave.lineTo (0.08f + t * 0.84f,
                     0.5f - 0.34f * std::sin (t * juce::MathConstants<float>::twoPi));
    }

    return strokeOf (wave, tokens::icon::regular);
}

juce::Path waveSaw()
{
    // Two wide teeth, not three narrow ones: at icon size, narrow teeth blur
    // into a zigzag and stop reading as a sawtooth.
    juce::Path wave;
    wave.startNewSubPath (0.10f, 0.82f);
    wave.lineTo (0.46f, 0.18f);
    wave.lineTo (0.46f, 0.82f);
    wave.lineTo (0.82f, 0.18f);
    wave.lineTo (0.82f, 0.82f);
    wave.lineTo (0.92f, 0.64f);
    return strokeOf (wave, tokens::icon::regular);
}

juce::Path waveSquare()
{
    juce::Path wave;
    wave.startNewSubPath (0.08f, 0.5f);
    wave.lineTo (0.08f, 0.18f);
    wave.lineTo (0.36f, 0.18f);
    wave.lineTo (0.36f, 0.82f);
    wave.lineTo (0.64f, 0.82f);
    wave.lineTo (0.64f, 0.18f);
    wave.lineTo (0.92f, 0.18f);
    wave.lineTo (0.92f, 0.5f);
    return strokeOf (wave, tokens::icon::regular);
}

juce::Path waveTriangle()
{
    juce::Path wave;
    wave.startNewSubPath (0.08f, 0.66f);
    wave.lineTo (0.29f, 0.2f);
    wave.lineTo (0.5f, 0.8f);
    wave.lineTo (0.71f, 0.2f);
    wave.lineTo (0.92f, 0.66f);
    return strokeOf (wave, tokens::icon::regular);
}

// --- effects -----------------------------------------------------------------

juce::Path effectFilter()
{
    // A lowpass response curve: flat, then a knee, then a fall.
    juce::Path curve;
    curve.startNewSubPath (0.08f, 0.34f);
    curve.lineTo (0.44f, 0.34f);
    curve.quadraticTo (0.60f, 0.34f, 0.66f, 0.5f);
    curve.lineTo (0.92f, 0.86f);
    return strokeOf (curve, tokens::icon::regular);
}

juce::Path effectReverb()
{
    juce::Path p;

    // An impulse and its decaying reflections.
    p.addRoundedRectangle (0.12f, 0.18f, 0.09f, 0.64f, 0.04f);

    for (int i = 1; i <= 3; ++i)
    {
        const auto x = 0.12f + (float) i * 0.22f;
        const auto shrink = 0.16f * (float) i;
        p.addRoundedRectangle (x, 0.18f + shrink, 0.07f, 0.64f - shrink * 2.0f, 0.03f);
    }

    return p;
}

juce::Path effectDelay()
{
    juce::Path p;
    p.addRoundedRectangle (0.10f, 0.22f, 0.09f, 0.56f, 0.04f);
    p.addRoundedRectangle (0.44f, 0.32f, 0.08f, 0.36f, 0.04f);
    p.addRoundedRectangle (0.76f, 0.40f, 0.07f, 0.20f, 0.03f);
    return p;
}

juce::Path effectDrive()
{
    // A soft-clipped sine: flattened at the top and bottom.
    juce::Path wave;
    wave.startNewSubPath (0.08f, 0.5f);

    for (int i = 1; i <= 32; ++i)
    {
        const auto t = (float) i / 32.0f;
        const auto raw = std::sin (t * juce::MathConstants<float>::twoPi) * 2.0f;
        wave.lineTo (0.08f + t * 0.84f, 0.5f - 0.34f * juce::jlimit (-1.0f, 1.0f, raw));
    }

    return strokeOf (wave, tokens::icon::regular);
}

juce::Path effectChorus()
{
    juce::Path wave;

    for (int line = 0; line < 2; ++line)
    {
        const auto offset = line == 0 ? -0.12f : 0.12f;
        wave.startNewSubPath (0.08f, 0.5f + offset);

        for (int i = 1; i <= 24; ++i)
        {
            const auto t = (float) i / 24.0f;
            wave.lineTo (0.08f + t * 0.84f,
                         0.5f + offset - 0.2f * std::sin ((t + (float) line * 0.3f)
                                                          * juce::MathConstants<float>::twoPi));
        }
    }

    return strokeOf (wave, tokens::icon::regular);
}

juce::Path effectEq()
{
    juce::Path p;
    const float heights[] = { 0.34f, 0.62f, 0.46f, 0.74f };

    for (int i = 0; i < 4; ++i)
    {
        const auto x = 0.12f + (float) i * 0.21f;
        p.addRoundedRectangle (x, 0.88f - heights[i], 0.11f, heights[i], 0.04f);
    }

    return p;
}

// --- views -------------------------------------------------------------------

namespace
{

juce::Path magnifier()
{
    juce::Path glass;
    glass.addEllipse (0.14f, 0.14f, 0.5f, 0.5f);

    auto p = strokeOf (glass, tokens::icon::regular);
    p.addPath (strokedLine (0.60f, 0.60f, 0.86f, 0.86f, tokens::icon::bold));
    return p;
}

} // namespace

juce::Path zoomIn()
{
    auto p = magnifier();
    p.addRoundedRectangle (0.35f, 0.25f, 0.08f, 0.28f, 0.03f);
    p.addRoundedRectangle (0.25f, 0.35f, 0.28f, 0.08f, 0.03f);
    return p;
}

juce::Path zoomOut()
{
    auto p = magnifier();
    p.addRoundedRectangle (0.25f, 0.35f, 0.28f, 0.08f, 0.03f);
    return p;
}

juce::Path fitToContent()
{
    juce::Path p;
    p.addRoundedRectangle (0.10f, 0.18f, 0.08f, 0.64f, 0.03f);
    p.addRoundedRectangle (0.82f, 0.18f, 0.08f, 0.64f, 0.03f);
    p.addPath (strokedLine (0.26f, 0.5f, 0.74f, 0.5f, tokens::icon::regular));
    p.addTriangle (0.26f, 0.5f, 0.40f, 0.36f, 0.40f, 0.64f);
    p.addTriangle (0.74f, 0.5f, 0.60f, 0.36f, 0.60f, 0.64f);
    return p;
}

juce::Path rowsShorter()
{
    // Three rules: more rows fit, so each is shorter. The metaphor is the
    // content rather than an arrow, because an arrow here would be a fourth
    // thing in this toolbar pointing up and down.
    juce::Path p;

    for (const auto y : { 0.22f, 0.46f, 0.70f })
        p.addRoundedRectangle (0.16f, y, 0.68f, 0.08f, 0.03f);

    return p;
}

juce::Path rowsTaller()
{
    juce::Path p;

    for (const auto y : { 0.26f, 0.62f })
        p.addRoundedRectangle (0.16f, y, 0.68f, 0.08f, 0.03f);

    return p;
}

juce::Path fitRows()
{
    // fitToContent turned through a right angle, deliberately: it is the same
    // command on the other axis, and it should be the same picture.
    juce::Path p;
    p.addRoundedRectangle (0.18f, 0.10f, 0.64f, 0.08f, 0.03f);
    p.addRoundedRectangle (0.18f, 0.82f, 0.64f, 0.08f, 0.03f);
    p.addPath (strokedLine (0.5f, 0.26f, 0.5f, 0.74f, tokens::icon::regular));
    p.addTriangle (0.5f, 0.26f, 0.36f, 0.40f, 0.64f, 0.40f);
    p.addTriangle (0.5f, 0.74f, 0.36f, 0.60f, 0.64f, 0.60f);
    return p;
}

juce::Path automation()
{
    juce::Path line;
    line.startNewSubPath (0.10f, 0.76f);
    line.lineTo (0.36f, 0.28f);
    line.lineTo (0.64f, 0.60f);
    line.lineTo (0.90f, 0.20f);

    auto p = strokeOf (line, tokens::icon::regular);
    p.addEllipse (0.30f, 0.22f, 0.12f, 0.12f);
    p.addEllipse (0.58f, 0.54f, 0.12f, 0.12f);
    return p;
}

// --- helpers -----------------------------------------------------------------

juce::Path fitted (juce::Path path, juce::Rectangle<float> bounds)
{
    path.scaleToFit (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), true);
    return path;
}

void draw (juce::Graphics& g, const juce::Path& path, juce::Rectangle<float> bounds,
           juce::Colour colour)
{
    g.setColour (colour);
    g.fillPath (fitted (path, bounds));
}

/** A stack of saved sounds: three sheets, the front one lifted.

    Not a floppy disk and not a folder. Nothing here saves - the presets ship
    with the build - so an icon that says "write" would promise a thing the
    button does not do.
*/
juce::Path preset()
{
    juce::Path lines;
    lines.startNewSubPath (0.22f, 0.34f);
    lines.lineTo (0.78f, 0.34f);
    lines.startNewSubPath (0.22f, 0.52f);
    lines.lineTo (0.78f, 0.52f);
    lines.startNewSubPath (0.22f, 0.70f);
    lines.lineTo (0.56f, 0.70f);

    return strokeOf (lines, tokens::icon::regular);
}

std::vector<NamedIcon> all()
{
    return {
        { "play", play }, { "pause", pause }, { "stop", stop }, { "rewind", rewind },
        { "record", record }, { "loop", loop },

        { "plus", plus }, { "minus", minus }, { "trash", trash }, { "duplicate", duplicate },
        { "pencil", pencil }, { "magnet", magnet }, { "pointer", pointer }, { "eraser", eraser },
        { "scissors", scissors }, { "dice", dice }, { "quantize", quantize },

        { "mute", mute }, { "solo", solo }, { "power", power }, { "lock", lock },
        { "check", check }, { "chevronUp", chevronUp }, { "chevronDown", chevronDown },
        { "chevronLeft", chevronLeft }, { "chevronRight", chevronRight }, { "grip", grip },

        { "waveSine", waveSine }, { "waveSaw", waveSaw }, { "waveSquare", waveSquare },
        { "waveTriangle", waveTriangle },

        { "effectFilter", effectFilter }, { "effectReverb", effectReverb },
        { "effectDelay", effectDelay }, { "effectDrive", effectDrive },
        { "effectChorus", effectChorus }, { "effectEq", effectEq },

        { "zoomIn", zoomIn }, { "zoomOut", zoomOut }, { "fitToContent", fitToContent },
        { "rowsShorter", rowsShorter }, { "rowsTaller", rowsTaller }, { "fitRows", fitRows },
        { "automation", automation }, { "preset", preset },
    };
}

} // namespace dew::icons
