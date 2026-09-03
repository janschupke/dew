#include "ui/TimelinePaint.h"

#include "ui/design/Tokens.h"

namespace dew::timelinePaint
{

using namespace tokens;

namespace
{

/** Below this, one line per step is a wall rather than a grid. */
constexpr double subBeatCutoffPx = 6.0;

juce::Colour playheadColour (float brightness)
{
    // Between dimmed and opaque rather than between two colours: a stopped
    // playhead is the same yellow, stated less strongly.
    const auto alpha = juce::jmap (juce::jlimit (0.0f, 1.0f, brightness), emphasis::dimmed, 1.0f);

    return colour::playhead.withAlpha (alpha);
}

} // namespace

void verticalGrid (juce::Graphics& g, const TimelineView& timeline, juce::Range<int> steps,
                   int stepsPerBar, int stepsPerBeat, float originX, juce::Range<float> y,
                   float rightEdge)
{
    const auto bar = juce::jmax (1, stepsPerBar);
    const auto beat = juce::jmax (1, stepsPerBeat);

    for (int step = steps.getStart(); step <= steps.getEnd(); ++step)
    {
        const auto x = originX + timeline.xForStep ((double) step);

        if (x > rightEdge)
            break;

        if (step % bar == 0)
            g.setColour (colour::dividerStrong);
        else if (step % beat == 0)
            g.setColour (colour::divider);
        else if (timeline.pixelsPerStep >= subBeatCutoffPx)
            g.setColour (colour::divider.withAlpha (emphasis::subdued));
        else
            continue;

        g.drawVerticalLine ((int) x, y.getStart(), y.getEnd());
    }
}

void playheadColumn (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (colour::playhead.withAlpha (emphasis::wash));
    g.fillRect (bounds);
}

void playheadLine (juce::Graphics& g, float x, juce::Range<float> y, float brightness)
{
    // stroke::regular, not a pixel: the four playheads were 1, 1, 1.5 and 2
    // wide, and the 1.5 is the one that was looked at - thin enough not to hide
    // a note under it, thick enough to find across a full arrangement.
    g.setColour (playheadColour (brightness));
    g.fillRect (x, y.getStart(), stroke::regular, y.getLength());
}

void playheadHead (juce::Graphics& g, float x, float baselineY, float brightness)
{
    g.setColour (playheadColour (brightness));

    juce::Path head;
    head.addTriangle (x - playheadHeadHalfWidth, baselineY - playheadHeadHeight,
                      x + playheadHeadHalfWidth, baselineY - playheadHeadHeight, x, baselineY);
    g.fillPath (head);
}

} // namespace dew::timelinePaint
