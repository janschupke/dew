// Zoom, fit, lane height, automation - and the two helpers.
//
// One of three translation units holding dew's icon catalog, which was 682
// lines in one file. The shared stroke helpers are IconStroke.h.
//
// The icons about looking rather than doing. `fitted` and `draw` are at the
// bottom because every caller reaches an icon through one of them, and
// `preset` is here because it is a magnifier over a shape.

#include "ui/design/Icons.h"

#include "ui/design/icons/IconStroke.h"
#include "ui/design/Tokens.h"

namespace dew::icons
{

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

juce::Path search()
{
    // The magnifier with nothing laid over it. zoomIn and zoomOut are this
    // path plus a plus or a minus, so the helper was already here and this is
    // the shape it was factored out of.
    return magnifier();
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
        { "play", play },
        { "pause", pause },
        { "stop", stop },
        { "rewind", rewind },
        { "record", record },
        { "loop", loop },

        { "plus", plus },
        { "minus", minus },
        { "trash", trash },
        { "duplicate", duplicate },
        { "pencil", pencil },
        { "magnet", magnet },
        { "pointer", pointer },
        { "eraser", eraser },
        { "scissors", scissors },
        { "dice", dice },
        { "quantize", quantize },
        { "open", open },
        { "reset", reset },
        { "palette", palette },
        { "pattern", pattern },

        { "power", power },
        { "lock", lock },
        { "check", check },
        { "cross", cross },
        { "chevronUp", chevronUp },
        { "chevronDown", chevronDown },
        { "chevronLeft", chevronLeft },
        { "chevronRight", chevronRight },
        { "grip", grip },

        { "waveSine", waveSine },
        { "waveSaw", waveSaw },
        { "waveSquare", waveSquare },
        { "waveTriangle", waveTriangle },

        { "instrumentSynth", instrumentSynth },
        { "instrumentAudio", instrumentAudio },
        { "instrumentSoundFont", instrumentSoundFont },

        { "effectFilter", effectFilter },
        { "effectReverb", effectReverb },
        { "effectDelay", effectDelay },
        { "effectDrive", effectDrive },
        { "effectDistortion", effectDistortion },
        { "effectChorus", effectChorus },
        { "effectPhaser", effectPhaser },
        { "effectEq", effectEq },
        { "effectCompressor", effectCompressor },
        { "effectLimiter", effectLimiter },

        { "zoomIn", zoomIn },
        { "zoomOut", zoomOut },
        { "search", search },
        { "fitToContent", fitToContent },
        { "rowsShorter", rowsShorter },
        { "rowsTaller", rowsTaller },
        { "fitRows", fitRows },
        { "automation", automation },
        { "preset", preset },
        { "shapeLine", shapeLine },
        { "shapeCurve", shapeCurve },
        { "shapeStep", shapeStep },
    };
}

} // namespace dew::icons
