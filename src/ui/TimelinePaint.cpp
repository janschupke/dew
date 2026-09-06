#include "ui/TimelinePaint.h"

#include "ui/GridDensity.h"
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
                   float rightEdge, int snapSteps)
{
    const auto bar = juce::jmax (1, stepsPerBar);
    const auto beat = juce::jmax (1, stepsPerBeat);

    // Only where it says something: a cell as coarse as a beat is already a
    // beat line, and one too narrow to see is the wall the step cutoff exists
    // to prevent.
    const auto snap = snapSteps > 1 && snapSteps < beat
                              && timeline.pixelsPerStep * (double) snapSteps >= subBeatCutoffPx
                          ? snapSteps
                          : 0;

    // How many bars and beats apart the marks have to be. Both of these used
    // to be 1 unconditionally - a line at every bar and every beat whatever
    // the zoom - which is what turned the far end of the zoom range into fill.
    const auto barsPerLine = gridDensity::strideFor (timeline.pixelsPerStep * (double) bar,
                                                     gridDensity::barSpacingPx);
    const auto beatsShow = timeline.pixelsPerStep * (double) beat >= gridDensity::beatSpacingPx;

    for (int step = steps.getStart(); step <= steps.getEnd(); ++step)
    {
        const auto x = originX + timeline.xForStep ((double) step);

        if (x > rightEdge)
            break;

        // Ordered by weight, and a strided-out bar is DROPPED rather than
        // demoted to the tier below. Demoting it looks like the kinder answer
        // and is not: the whole reason a stride was needed is that the bars are
        // closer together than they can be read at, and drawing all of them a
        // shade fainter leaves exactly as many lines on the canvas. What tells
        // the reader the spacing is the ruler's numbers, which stride with it.
        if (step % (bar * barsPerLine) == 0)
            g.setColour (colour::dividerStrong);
        else if (step % beat == 0 && beatsShow)
            g.setColour (colour::divider);
        else if (snap > 0 && step % snap == 0)
            g.setColour (colour::divider.withAlpha (emphasis::dimmed));
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
