#include "ui/TimelineRuler.h"

#include <cmath>

#include "ui/TimelinePaint.h"
#include "ui/design/Cursors.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace ruler
{

void paint (juce::Graphics& g, juce::Rectangle<int> bounds, const TimelineView& timeline,
            const Style& style)
{
    if (bounds.isEmpty())
        return;

    g.setColour (colour::surface);
    g.fillRect (bounds);

    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (bounds);

    const auto stepsPerBar = juce::jmax (1, style.stepsPerBar);
    const auto stepsPerBeat = juce::jmax (1, stepsPerBar / juce::jmax (1, style.beatsPerBar));

    // Same rule the grid below applies: only where the cell says something the
    // beat tick does not, and only where there is room to see it.
    const auto snapSteps = style.snapSteps > 1 && style.snapSteps < stepsPerBeat
                                   && timeline.pixelsPerStep * (double) style.snapSteps >= 10.0
                               ? style.snapSteps
                               : 0;

    // Before the bar lines and numbers, so they stay legible on top of it -
    // the order the playlist's own strip already uses.
    if (style.hasSelection())
    {
        const auto fromX = (float) bounds.getX() + timeline.xForStep (style.selectionStartSteps);
        const auto toX = (float) bounds.getX() + timeline.xForStep (style.selectionEndSteps);

        g.setColour (colour::accentMuted);
        g.fillRect (juce::Rectangle<float> (fromX, (float) bounds.getY(),
                                            juce::jmax (1.0f, toX - fromX),
                                            (float) bounds.getHeight()));

        // An edge at each end, so where a span STOPS is readable even when it
        // runs off the side of the view.
        g.setColour (colour::accent);
        g.fillRect (juce::Rectangle<float> (fromX, (float) bounds.getY(), 2.0f,
                                            (float) bounds.getHeight()));
        g.fillRect (juce::Rectangle<float> (toX - 2.0f, (float) bounds.getY(), 2.0f,
                                            (float) bounds.getHeight()));
    }

    // Unclamped, so the ruler does not stop numbering halfway across the
    // window when the material is shorter than the view.
    const auto range = timeline.visibleStepRange ((float) bounds.getWidth());

    // A rung above the caption the rest of the ladder's floor sits on. A bar
    // number is read at a glance from across a desk while your hands are
    // elsewhere, which is a different job from a knob's caption - and 11px in
    // textSecondary was the smallest thing in the window doing the most
    // frequently read work in it.
    g.setFont (type::font (type::small));

    for (int step = range.getStart(); step <= range.getEnd(); ++step)
    {
        const auto x = (float) bounds.getX() + timeline.xForStep ((double) step);

        if (x > (float) bounds.getRight())
            break;

        const auto beyond = step >= style.totalSteps;

        if (step % stepsPerBar == 0)
        {
            g.setColour (beyond ? colour::dividerStrong.withAlpha (emphasis::subdued)
                                : colour::dividerStrong);
            g.drawVerticalLine ((int) x, (float) bounds.getY(), (float) bounds.getBottom());

            // Bar numbers only where there is room for them to be readable.
            if (timeline.pixelsPerStep * stepsPerBar >= 28.0)
            {
                g.setColour (beyond ? colour::textDisabled : colour::textSecondary);
                g.drawText (
                    juce::String (step / stepsPerBar + 1),
                    juce::Rectangle<int> ((int) x + 3, bounds.getY(), 40, bounds.getHeight()),
                    juce::Justification::centredLeft, false);
            }
        }
        else if (step % stepsPerBeat == 0 && timeline.pixelsPerStep * stepsPerBeat >= 10.0)
        {
            g.setColour (beyond ? colour::divider.withAlpha (emphasis::subdued) : colour::divider);
            g.drawVerticalLine ((int) x, (float) bounds.getBottom() - 6.0f,
                                (float) bounds.getBottom());
        }
        else if (snapSteps > 0 && step % snapSteps == 0)
        {
            // Half the beat tick's reach, and dimmer: it is a subdivision of the
            // thing above it and has to read as one.
            g.setColour (colour::divider.withAlpha (beyond ? emphasis::wash : emphasis::subdued));
            g.drawVerticalLine ((int) x, (float) bounds.getBottom() - 3.0f,
                                (float) bounds.getBottom());
        }
    }

    // A head marking the position, so where the transport is can be seen and
    // grabbed on the ruler itself rather than only inferred from the grid.
    if (style.playheadSteps >= 0.0)
    {
        const auto x = (float) bounds.getX() + timeline.xForStep (style.playheadSteps);

        const auto margin = timelinePaint::playheadHeadHalfWidth;

        if (x >= (float) bounds.getX() - margin && x <= (float) bounds.getRight() + margin)
            timelinePaint::playheadHead (g, x, (float) bounds.getBottom(),
                                         style.playheadBrightness);
    }

    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (bounds.getBottom() - 1, (float) bounds.getX(), (float) bounds.getRight());
}

double stepForClick (int x, juce::Rectangle<int> bounds, const TimelineView& timeline,
                     int totalSteps)
{
    const auto raw = timeline.stepForX ((float) (x - bounds.getX()));

    // Clamped to the material. Dragging off the end of a short pattern should
    // park at its end, not seek into space that has nothing in it.
    return juce::jlimit (0.0, (double) juce::jmax (0, totalSteps), raw);
}

// --- the gesture -------------------------------------------------------------

GestureContext Gesture::contextOrDefault() const
{
    return context != nullptr ? context() : GestureContext();
}

double Gesture::unitAt (int x) const
{
    return unitForX != nullptr ? unitForX (x) : 0.0;
}

int Gesture::snapUnit (double raw, bool roundUp) const
{
    const auto ctx = contextOrDefault();
    const auto snap = juce::jmax (1, ctx.snapUnits);
    const auto units = raw / (double) snap;

    return juce::jlimit (0, juce::jmax (0, ctx.totalUnits),
                         (int) (roundUp ? std::ceil (units) : std::floor (units)) * snap);
}

void Gesture::applySpan (double from, double to)
{
    if (onRangeChanged == nullptr)
        return;

    const auto ctx = contextOrDefault();
    const auto snap = juce::jmax (1, ctx.snapUnits);

    auto lo = snapUnit (juce::jmin (from, to), false);
    auto hi = snapUnit (juce::jmax (from, to), true);

    if (hi <= lo)
    {
        lo = juce::jmax (0, juce::jmin (lo, ctx.totalUnits - snap));
        hi = lo + snap;
    }

    onRangeChanged ({ lo, hi });
}

bool Gesture::mouseDown (const juce::MouseEvent& event)
{
    // A right press on a ruler used to SEEK - it fell straight through to the
    // scrubbing branch, so aiming at a menu that is not there moved the
    // playhead and, in a take, moved where the take was going. The rule dew
    // holds everywhere else is that the right button opens a menu or does
    // nothing; a ruler has no menu, so it does nothing.
    //
    // Returning true still, because the press is CONSUMED: falling through
    // would hand it to whatever is under the ruler.
    if (event.mods.isPopupMenu())
    {
        mode = Mode::none;
        return true;
    }

    anchorRaw = unitAt (event.x);
    clearsIfUnmoved = false;

    if (event.mods.isShiftDown())
    {
        mode = Mode::selecting;
        clearsIfUnmoved = true;
        applySpan (anchorRaw, anchorRaw);
        return true;
    }

    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        mode = Mode::selecting;

        const auto ctx = contextOrDefault();
        const auto playhead = juce::jlimit (0.0, (double) juce::jmax (0, ctx.totalUnits),
                                            ctx.playheadUnits);

        applySpan (playhead, anchorRaw);
        return true;
    }

    mode = Mode::scrubbing;

    if (onSeek != nullptr)
        onSeek (anchorRaw);

    return true;
}

bool Gesture::mouseDrag (const juce::MouseEvent& event)
{
    if (mode == Mode::scrubbing)
    {
        if (onSeek != nullptr)
            onSeek (unitAt (event.x));

        return true;
    }

    if (mode != Mode::selecting)
        return false;

    // The anchor is always where the press landed, so a mod-press that moves
    // stops meaning "from the playhead" and starts meaning "from here". The
    // playhead only ever names the far end of the press itself, which is why a
    // mod-drag restarts the span rather than extending one nobody chose.
    applySpan (anchorRaw, unitAt (event.x));
    return true;
}

bool Gesture::mouseUp (const juce::MouseEvent& event)
{
    if (mode == Mode::none)
        return false;

    const auto wasSelecting = mode == Mode::selecting;
    mode = Mode::none;

    // A shift-CLICK is how a span is taken back: it selects a unit on the way
    // down, and letting go without having moved means the user asked for
    // nothing rather than for that unit.
    if (wasSelecting && clearsIfUnmoved && ! event.mouseWasDraggedSinceMouseDown()
        && onRangeCleared != nullptr)
        onRangeCleared();

    return true;
}

bool Gesture::mouseDoubleClick (const juce::MouseEvent&)
{
    mode = Mode::none;

    if (onRangeCleared == nullptr)
        return false;

    onRangeCleared();
    return true;
}

} // namespace ruler

// -----------------------------------------------------------------------------

RulerStrip::RulerStrip()
{
    setMouseCursor (cursor::clickable);
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
    auto style = styleSource != nullptr ? styleSource() : ruler::Style();

    playhead.set (style.playing);
    style.playheadBrightness = playhead.brightness();

    ruler::paint (g, getLocalBounds(), timeline, style);
}

void RulerStrip::mouseDown (const juce::MouseEvent& event)
{
    prepareGesture();
    gesture.mouseDown (event);
}

void RulerStrip::mouseDrag (const juce::MouseEvent& event)
{
    prepareGesture();
    gesture.mouseDrag (event);
}

void RulerStrip::mouseUp (const juce::MouseEvent& event)
{
    gesture.mouseUp (event);
}

void RulerStrip::mouseDoubleClick (const juce::MouseEvent& event)
{
    gesture.mouseDoubleClick (event);
}

void RulerStrip::prepareGesture()
{
    // Bound on every press rather than once in the constructor: the sources
    // this reads through are set by the host after construction, and a strip
    // that captured them early would seek along a timeline it no longer has.
    if (gesture.unitForX != nullptr)
        return;

    gesture.unitForX = [this] (int x)
    {
        const auto& timeline = timelineSource != nullptr ? timelineSource() : fallback;
        const auto style = styleSource != nullptr ? styleSource() : ruler::Style();

        return ruler::stepForClick (x, getLocalBounds(), timeline, style.totalSteps);
    };

    gesture.context = [this]
    {
        const auto style = styleSource != nullptr ? styleSource() : ruler::Style();

        ruler::GestureContext ctx;

        // One beat, which is what a ruler drag has always snapped to - but
        // written as the meter says rather than as stepsPerBar / 4, which is a
        // beat only in 4/4.
        ctx.snapUnits = juce::jmax (1, style.stepsPerBar / juce::jmax (1, style.beatsPerBar));
        ctx.totalUnits = style.totalSteps;
        ctx.playheadUnits = juce::jmax (0.0, style.playheadSteps);

        return ctx;
    };

    gesture.onSeek = [this] (double steps)
    {
        if (onSeek != nullptr)
            onSeek (steps);
    };

    gesture.onRangeChanged = [this] (juce::Range<int> steps)
    {
        if (onRangeChanged != nullptr)
            onRangeChanged (steps);
    };

    gesture.onRangeCleared = [this]
    {
        if (onRangeCleared != nullptr)
            onRangeCleared();
    };
}

} // namespace dew
