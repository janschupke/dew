#include "TimelineRuler.h"

#include "design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace ruler
{

void paint (juce::Graphics& g, juce::Rectangle<int> bounds,
            const TimelineView& timeline, const Style& style)
{
    if (bounds.isEmpty())
        return;

    g.setColour (colour::surface);
    g.fillRect (bounds);

    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (bounds);

    const auto stepsPerBar = juce::jmax (1, style.stepsPerBar);
    const auto stepsPerBeat = juce::jmax (1, stepsPerBar / 4);

    // Before the bar lines and numbers, so they stay legible on top of it -
    // the order the playlist's own strip already uses.
    if (style.hasSelection())
    {
        const auto fromX = (float) bounds.getX() + timeline.xForStep (style.selectionStartSteps);
        const auto toX   = (float) bounds.getX() + timeline.xForStep (style.selectionEndSteps);

        g.setColour (colour::accentMuted);
        g.fillRect (juce::Rectangle<float> (fromX, (float) bounds.getY(),
                                            juce::jmax (1.0f, toX - fromX), (float) bounds.getHeight()));

        // An edge at each end, so where a span STOPS is readable even when it
        // runs off the side of the view.
        g.setColour (colour::accent);
        g.fillRect (juce::Rectangle<float> (fromX, (float) bounds.getY(), 2.0f, (float) bounds.getHeight()));
        g.fillRect (juce::Rectangle<float> (toX - 2.0f, (float) bounds.getY(), 2.0f, (float) bounds.getHeight()));
    }

    // Unclamped, so the ruler does not stop numbering halfway across the
    // window when the material is shorter than the view.
    const auto range = timeline.visibleStepRange ((float) bounds.getWidth());

    g.setFont (type::font (type::caption));

    for (int step = range.getStart(); step <= range.getEnd(); ++step)
    {
        const auto x = (float) bounds.getX() + timeline.xForStep ((double) step);

        if (x > (float) bounds.getRight())
            break;

        const auto beyond = step >= style.totalSteps;

        if (step % stepsPerBar == 0)
        {
            g.setColour (beyond ? colour::dividerStrong.withAlpha (0.35f) : colour::dividerStrong);
            g.drawVerticalLine ((int) x, (float) bounds.getY(), (float) bounds.getBottom());

            // Bar numbers only where there is room for them to be readable.
            if (timeline.pixelsPerStep * stepsPerBar >= 28.0)
            {
                g.setColour (beyond ? colour::textDisabled : colour::textSecondary);
                g.drawText (juce::String (step / stepsPerBar + 1),
                            juce::Rectangle<int> ((int) x + 3, bounds.getY(), 40, bounds.getHeight()),
                            juce::Justification::centredLeft, false);
            }
        }
        else if (step % stepsPerBeat == 0 && timeline.pixelsPerStep * stepsPerBeat >= 10.0)
        {
            g.setColour (beyond ? colour::divider.withAlpha (0.35f) : colour::divider);
            g.drawVerticalLine ((int) x, (float) bounds.getBottom() - 6.0f, (float) bounds.getBottom());
        }
    }

    // A head marking the position, so where the transport is can be seen and
    // grabbed on the ruler itself rather than only inferred from the grid.
    if (style.playheadSteps >= 0.0)
    {
        const auto x = (float) bounds.getX() + timeline.xForStep (style.playheadSteps);

        if (x >= (float) bounds.getX() - 6.0f && x <= (float) bounds.getRight() + 6.0f)
        {
            g.setColour (style.playing ? colour::playhead : colour::playhead.withAlpha (0.5f));

            juce::Path head;
            head.addTriangle (x - 5.0f, (float) bounds.getBottom() - 8.0f,
                              x + 5.0f, (float) bounds.getBottom() - 8.0f,
                              x, (float) bounds.getBottom());
            g.fillPath (head);
        }
    }

    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (bounds.getBottom() - 1, (float) bounds.getX(), (float) bounds.getRight());
}

double stepForClick (int x, juce::Rectangle<int> bounds, const TimelineView& timeline, int totalSteps)
{
    const auto raw = timeline.stepForX ((float) (x - bounds.getX()));

    // Clamped to the material. Dragging off the end of a short pattern should
    // park at its end, not seek into space that has nothing in it.
    return juce::jlimit (0.0, (double) juce::jmax (0, totalSteps), raw);
}

} // namespace ruler

// -----------------------------------------------------------------------------

RulerStrip::RulerStrip()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (motion::playheadHz);
}

RulerStrip::~RulerStrip()
{
    stopTimer();
}

void RulerStrip::timerCallback()
{
    if (styleSource == nullptr)
        return;

    const auto style = styleSource();

    if (juce::exactlyEqual (style.playheadSteps, lastPaintedPlayhead)
        && style.playing == lastPlaying)
        return;

    lastPaintedPlayhead = style.playheadSteps;
    lastPlaying = style.playing;
    repaint();
}

void RulerStrip::paint (juce::Graphics& g)
{
    const auto& timeline = timelineSource != nullptr ? timelineSource() : fallback;
    const auto style = styleSource != nullptr ? styleSource() : ruler::Style();

    ruler::paint (g, getLocalBounds(), timeline, style);
}

void RulerStrip::mouseDown (const juce::MouseEvent& event)
{
    seekTo (event);
}

void RulerStrip::mouseDrag (const juce::MouseEvent& event)
{
    seekTo (event);
}

void RulerStrip::seekTo (const juce::MouseEvent& event)
{
    if (onSeek == nullptr)
        return;

    const auto& timeline = timelineSource != nullptr ? timelineSource() : fallback;
    const auto style = styleSource != nullptr ? styleSource() : ruler::Style();

    onSeek (ruler::stepForClick (event.x, getLocalBounds(), timeline, style.totalSteps));
}

} // namespace dew
