// Waveforms, instruments and effect types.
//
// One of the translation units holding dew's icon catalog, which was 682 lines
// in one file. The shared stroke helpers are IconStroke.h.
//
// The four oscillator shapes, the three kinds of instrument and the ten
// effects. These are the icons that have to LOOK like the thing rather than
// stand for it, which is why a saw is drawn as a saw and not as the letter S.

#include "ui/design/Icons.h"

#include "ui/design/icons/IconStroke.h"
#include "ui/design/Tokens.h"

namespace dew::icons
{

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

// --- instruments -------------------------------------------------------------
//
// Three silhouettes rather than three drawings: a frame with knobs, a run of
// bars about a centre line, and a keyboard. They sit next to each other in the
// add-channel menu and that is the only place any of them has to work, so what
// matters is that no two share an outline at 14px.

juce::Path instrumentSynth()
{
    // A module with knobs on it: sound that is BUILT. Not a waveform - the
    // oscillator icons are already waveforms, and a synth that wore one of
    // them would say "sine" where it means "synthesiser".
    juce::Path frame;
    frame.addRoundedRectangle (0.12f, 0.20f, 0.76f, 0.60f, 0.09f);
    auto p = strokeOf (frame, tokens::icon::regular);

    for (int i = 0; i < 3; ++i)
        p.addEllipse (0.22f + (float) i * 0.24f, 0.42f, 0.16f, 0.16f);

    return p;
}

juce::Path instrumentAudio()
{
    // A sample's envelope: bars about a CENTRE line, where the EQ's bars stand
    // on the floor. The two are the same five rectangles otherwise, and that
    // one difference is what tells a recording from a filter bank.
    juce::Path p;
    const float heights[] = { 0.18f, 0.40f, 0.62f, 0.46f, 0.26f };

    for (int i = 0; i < 5; ++i)
    {
        const auto x = 0.12f + (float) i * 0.17f;
        p.addRoundedRectangle (x, 0.5f - heights[i] * 0.5f, 0.10f, heights[i], 0.04f);
    }

    return p;
}

juce::Path instrumentSoundFont()
{
    // Keys: a font is somebody else's instrument, recorded a note at a time.
    juce::Path body;
    body.addRoundedRectangle (0.12f, 0.24f, 0.76f, 0.52f, 0.06f);
    auto p = strokeOf (body, tokens::icon::regular);

    // Below the frame's stroke rather than under it: the top line is centred on
    // 0.24 and half a stroke wide either side, so keys drawn from there are
    // half buried and the three read as one filled box with two notches.
    p.addRoundedRectangle (0.31f, 0.30f, 0.11f, 0.26f, 0.03f);
    p.addRoundedRectangle (0.58f, 0.30f, 0.11f, 0.26f, 0.03f);

    juce::Path divider;
    divider.startNewSubPath (0.5f, 0.56f);
    divider.lineTo (0.5f, 0.76f);
    p.addPath (strokeOf (divider, tokens::icon::hair));

    return p;
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

juce::Path effectDistortion()
{
    // A hard-clipped square, against drive's rounded soft clip: the two sit
    // beside each other in the picker and have to be told apart there.
    juce::Path wave;
    wave.startNewSubPath (0.08f, 0.5f);
    wave.lineTo (0.08f, 0.20f);
    wave.lineTo (0.30f, 0.20f);
    wave.lineTo (0.30f, 0.80f);
    wave.lineTo (0.52f, 0.80f);
    wave.lineTo (0.52f, 0.20f);
    wave.lineTo (0.74f, 0.20f);
    wave.lineTo (0.74f, 0.80f);
    wave.lineTo (0.92f, 0.80f);
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
                         0.5f + offset
                             - 0.2f
                                   * std::sin ((t + (float) line * 0.3f)
                                               * juce::MathConstants<float>::twoPi));
        }
    }

    return strokeOf (wave, tokens::icon::regular);
}

juce::Path effectPhaser()
{
    // A flat response with two notches punched out of it. Narrow dips, set wide
    // apart, so there is a run of flat line before, between and after them:
    // drawn any closer together the two dips merge into a small w, which is
    // what the chorus already looks like at this size.
    juce::Path curve;
    curve.startNewSubPath (0.08f, 0.30f);

    for (int i = 1; i <= 48; ++i)
    {
        const auto t = (float) i / 48.0f;

        const auto notch = [t] (float centre)
        {
            const auto d = (t - centre) / 0.05f;
            return 0.44f * std::exp (-d * d);
        };

        curve.lineTo (0.08f + t * 0.84f, 0.30f + notch (0.26f) + notch (0.74f));
    }

    return strokeOf (curve, tokens::icon::regular);
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

juce::Path effectCompressor()
{
    // A transfer curve: a 45-degree rise that bends to a shallow slope, which is
    // literally what a ratio is.
    juce::Path curve;
    curve.startNewSubPath (0.10f, 0.90f);
    curve.lineTo (0.48f, 0.52f);
    curve.quadraticTo (0.56f, 0.44f, 0.66f, 0.40f);
    curve.lineTo (0.90f, 0.32f);
    return strokeOf (curve, tokens::icon::regular);
}

juce::Path effectLimiter()
{
    // The compressor's rise, cornered instead of bent, under a ceiling it does
    // not reach. The gap between the two is the whole icon: drawn any closer
    // the hairline and the knee merge into one bar at 26px, which is the size
    // an effect card actually draws it at.
    juce::Path p;
    p.startNewSubPath (0.10f, 0.92f);
    p.lineTo (0.54f, 0.44f);
    p.lineTo (0.90f, 0.44f);
    auto path = strokeOf (p, tokens::icon::regular);

    juce::Path ceilingMark;
    ceilingMark.startNewSubPath (0.10f, 0.14f);
    ceilingMark.lineTo (0.90f, 0.14f);
    path.addPath (strokeOf (ceilingMark, tokens::icon::hair));

    return path;
}

} // namespace dew::icons
