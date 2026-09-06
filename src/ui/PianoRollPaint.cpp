// =============================================================================
// PianoRollComponent's painting.
//
// The same class, a second translation unit, for the reason PlaylistPaint.cpp
// is one: the keyboard, the ruler, the note grid and the velocity lane share
// nothing with the gesture handling above them except the members they read,
// and together they were a fifth of a 1,874-line file.
// =============================================================================

#include "ui/PianoRollComponent.h"

#include "ui/PianoRollNotes.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/RandomizePanel.h"
#include "ui/TimelineRuler.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/TimelinePaint.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;
using namespace pianoRoll;

void PianoRollComponent::paintKeyboard (juce::Graphics& g)
{
    using namespace tokens;

    const auto keys = keyboardArea();

    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (keys);

    g.setColour (colour::wellDeep);
    g.fillRect (keys);

    const auto firstRow = juce::jmax (0, (int) (rows.scrollPx / rows.height));
    const auto lastRow = juce::jmin (numRows - 1,
                                     (int) ((rows.scrollPx + keys.getHeight()) / rows.height));

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto pitch = highestPitch - row;
        const auto y = (float) keys.getY() + (float) (row * rows.height) - (float) rows.scrollPx;
        const auto black = isBlackKey (pitch);

        auto colourValue = black ? colour::keyBlack : colour::keyWhite;

        // The key under the pointer lights while it sounds, so a click on the
        // keyboard is visibly doing something and not only audibly.
        if (pitch == auditionPitch)
            colourValue = colour::accent;

        g.setColour (colourValue);
        g.fillRect ((float) keys.getX(), y, (float) (keys.getWidth() - 1),
                    (float) (rows.height - 1));

        // Every C always, and every key once the rows are tall enough to hold a
        // name without the letters touching. That threshold is what makes a
        // taller row worth having here: at fourteen pixels the strip can only
        // say which octave you are in, and reading a voicing means counting
        // upwards from a C.
        const auto isC = pitch % semitonesPerOctave == 0;

        if (isC || rows.height >= size::pianoRowRoomy)
        {
            // Dark on a white key, light on a black one. One colour was fine
            // while only C was named, because C is never a black key.
            g.setColour (black ? colour::keyWhite : colour::textOnAccent);
            g.setFont (type::font (type::caption));
            g.drawText (noteName (pitch), keys.getX() + 3, (int) y, keys.getWidth() - 6,
                        rows.height, juce::Justification::centredLeft, false);
        }
    }

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (keys.getRight() - 1, (float) keys.getY(), (float) keys.getBottom());
}

void PianoRollComponent::paintRuler (juce::Graphics& g)
{
    using namespace tokens;

    const auto meter = Meter::of (document.getState());

    ruler::Style style;
    style.stepsPerBar = meter.stepsPerBar();
    style.beatsPerBar = meter.beatsPerBar;
    style.totalSteps = numSteps();
    style.playing = engine.isPlaying();

    if (engine.getMode() == Transport::Mode::pattern)
        style.playheadSteps = (double) ((int) engine.getPlayheadSteps()
                                        % juce::jmax (1, numSteps()));

    if (editorState.hasStepSelection())
    {
        // Not `selection`: this class has a member of that name, and the CI
        // preset builds with -Werror on -Wshadow.
        const auto steps = editorState.getSelectedStepRange();
        style.selectionStartSteps = (double) steps.getStart();
        style.selectionEndSteps = (double) steps.getEnd();
    }

    // The same ruler the playlist and the channel rack draw. Three views used
    // to hand-roll three of these, and no two of them behaved alike.
    ruler::paint (g, rulerArea(), timeline, style);

    // The corner over the keyboard, which the shared ruler knows nothing about.
    g.setColour (colour::surface);
    g.fillRect (0, 0, size::gutterKeyboard, size::rulerHeight);
    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (size::rulerHeight - 1, 0.0f, (float) size::gutterKeyboard);
}

void PianoRollComponent::paintNotes (juce::Graphics& g)
{
    using namespace tokens;

    const auto area = noteArea();

    g.setColour (colour::wellDeep);
    g.fillRect (area);

    // Clipped to the note area, not to the full width. This used to start at
    // x = 0, which included the keyboard gutter, so any note scrolled past the
    // left edge was painted straight over the keys.
    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area);

    const auto meter = Meter::of (document.getState());
    const auto stepsPerBeat = meter.stepsPerBeat;
    const auto stepsPerBar = meter.stepsPerBar();

    // --- rows ----------------------------------------------------------------
    const auto firstRow = juce::jmax (0, (int) (rows.scrollPx / rows.height));
    const auto lastRow = juce::jmin (numRows - 1,
                                     (int) ((rows.scrollPx + area.getHeight()) / rows.height));

    // Where the last pitch row ends. Only below the note area on a window tall
    // enough to show all 97 rows at once, but if it ever is, that strip should
    // be marked out rather than left as bare background.
    const auto rowsBottom = (float) area.getY() + (float) (numRows * rows.height)
                            - (float) rows.scrollPx;

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto pitch = highestPitch - row;
        const auto y = (float) area.getY() + (float) (row * rows.height) - (float) rows.scrollPx;

        if (isBlackKey (pitch))
        {
            g.setColour (colour::well);
            g.fillRect ((float) area.getX(), y, (float) area.getWidth(), (float) rows.height);
        }

        g.setColour (pitch % 12 == 0 ? colour::dividerStrong
                                     : colour::divider.withAlpha (emphasis::subdued));
        g.drawHorizontalLine ((int) y, (float) area.getX(), (float) area.getRight());
    }

    // --- columns -------------------------------------------------------------
    // The UNCLAMPED range: bar lines carry on to the edge of the window even
    // where the pattern has ended, so the grid never stops mid-view.
    const auto steps = numSteps();
    const auto range = timeline.visibleStepRange (contentWidth());

    timelinePaint::verticalGrid (
        g, timeline, range, stepsPerBar, stepsPerBeat, (float) size::gutterKeyboard,
        { (float) area.getY(), (float) area.getBottom() }, (float) area.getRight());

    // Past the end of the pattern is still drawn - it is just dimmed, with the
    // end itself marked. It used to be hatched over, which turned every window
    // wider than the music into a dead rectangle.
    const auto endX = (float) size::gutterKeyboard + timeline.xForStep ((double) steps);

    if (endX < (float) area.getRight())
        paint::beyondEnd (g,
                          juce::Rectangle<float> (endX, (float) area.getY(),
                                                  (float) area.getRight() - endX,
                                                  (float) area.getHeight())
                              .toNearestInt(),
                          endX);

    // Below the lowest pitch, for the same reason and in the same idiom.
    if (rowsBottom < (float) area.getBottom())
        paint::inertArea (g, juce::Rectangle<float> ((float) area.getX(), rowsBottom,
                                                     (float) area.getWidth(),
                                                     (float) area.getBottom() - rowsBottom)
                                 .toNearestInt());

    // --- notes ---------------------------------------------------------------
    const auto pattern = currentPattern();
    const auto channelId = editorState.getSelectedChannelId();
    const auto colourForChannel = channelColour();

    for (const auto& note : pattern)
    {
        if (! note.hasType (ids::NOTE))
            continue;

        const auto bounds = boundsForNote (note).reduced (0.5f, 1.0f);

        if (! bounds.intersects (area.toFloat()))
            continue;

        if ((int) note[ids::ch] != channelId)
        {
            // Other channels' notes are context, not something editable here.
            g.setColour (colour::dividerStrong.withAlpha (emphasis::subdued));
            g.fillRoundedRectangle (bounds, radius::xs);
            continue;
        }

        // Velocity is visible on the note itself, not only in the lane, so a
        // quiet note reads as quiet while you are writing the melody.
        const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);

        // A silent note is stated as faintly as anything else that is there but
        // not sounding; a full-velocity one is stated completely.
        g.setColour (
            colourForChannel.withAlpha (emphasis::subdued + (1.0f - emphasis::subdued) * velocity));
        g.fillRoundedRectangle (bounds, radius::xs);

        g.setColour (isSelected (note) ? colour::textPrimary
                                       : colourForChannel.brighter (emphasis::edgeLift));
        g.drawRoundedRectangle (bounds, radius::xs,
                                isSelected (note) ? stroke::regular : stroke::hairline);
    }

    // --- position indicator --------------------------------------------------
    // The moving line, and only while the transport is moving - see
    // timelinePaint::showsPlayheadLine. Where playback will begin is said by
    // the head on the ruler above, which is what a click there moves.
    if (engine.getMode() == Transport::Mode::pattern)
    {
        const auto playing = engine.isPlaying();
        const auto step = (double) ((int) engine.getPlayheadSteps() % steps);
        const auto x = (float) size::gutterKeyboard + timeline.xForStep (step);

        playhead.set (playing);

        if (playing)
            timelinePaint::playheadColumn (g,
                                           { x, (float) area.getY(), (float) timeline.pixelsPerStep,
                                             (float) area.getHeight() });

        if (timelinePaint::showsPlayheadLine (playhead.brightness()))
            timelinePaint::playheadLine (g, x, { (float) area.getY(), (float) area.getBottom() },
                                         playhead.brightness());
    }

    // --- rubber band ---------------------------------------------------------
    if (gesture == Gesture::selecting && ! rubberBand.isEmpty())
    {
        g.setColour (colour::accent.withAlpha (emphasis::wash));
        g.fillRect (rubberBand);
        g.setColour (colour::accent);
        g.drawRect (rubberBand, stroke::hairlinePx);
    }

    // Last, and clipped to the note area: the keyboard's own position has to be
    // findable over whatever is under it.
    paint::cursorOutline (g, boundsForCell (cursor.getPosition().x, cursor.getPosition().y),
                          cursor.isPlaced());
}

void PianoRollComponent::paintVelocityLane (juce::Graphics& g)
{
    using namespace tokens;

    const auto area = velocityArea();

    g.setColour (colour::well);
    g.fillRect (area);
    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (area.getY(), 0.0f, (float) getWidth());

    // Label gutter, so the lane is identifiable rather than a mystery strip.
    paint::sectionHeading (g, { 0, area.getY(), size::gutterKeyboard, area.getHeight() }, "VEL",
                           juce::Justification::centred);

    const auto channelId = editorState.getSelectedChannelId();
    const auto colourForChannel = channelColour();
    const auto barWidth = (float) juce::jlimit (2.0, 14.0, timeline.pixelsPerStep * 0.7);

    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area);

    // Bar lines, so a velocity bar can be read against the same grid as the
    // note it belongs to. The lane had no grid at all, which made it hard to
    // tell which bar went with which note once the view was zoomed out.
    const auto meter = Meter::of (document.getState());
    const auto stepsPerBar = meter.stepsPerBar();
    const auto steps = numSteps();

    const auto laneRange = timeline.visibleStepRange (contentWidth());

    for (int step = laneRange.getStart(); step <= laneRange.getEnd(); ++step)
    {
        if (step % stepsPerBar != 0)
            continue;

        const auto lineX = (float) size::gutterKeyboard + timeline.xForStep ((double) step);

        if (lineX > (float) area.getRight())
            break;

        g.setColour (step >= steps ? colour::dividerStrong.withAlpha (emphasis::subdued)
                                   : colour::dividerStrong);
        g.drawVerticalLine ((int) lineX, (float) area.getY(), (float) area.getBottom());
    }

    const auto laneEndX = (float) size::gutterKeyboard + timeline.xForStep ((double) steps);

    if (laneEndX < (float) area.getRight())
        paint::beyondEnd (g,
                          juce::Rectangle<float> (laneEndX, (float) area.getY(),
                                                  (float) area.getRight() - laneEndX,
                                                  (float) area.getHeight())
                              .toNearestInt(),
                          laneEndX);

    for (const auto& note : currentPattern())
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);
        const auto x = (float) size::gutterKeyboard
                       + timeline.xForStep ((double) (int) note[ids::step]);

        if (x < (float) area.getX() - barWidth || x > (float) area.getRight())
            continue;

        const auto height = velocity * (float) (area.getHeight() - 8);
        const auto bar = juce::Rectangle<float> (x + 1.0f, (float) area.getBottom() - 4.0f - height,
                                                 barWidth, height);

        g.setColour (isSelected (note) ? colour::textPrimary
                                       : colourForChannel.withAlpha (emphasis::strong));
        g.fillRect (bar);
        g.setColour (colour::wellDeep);
        g.fillEllipse (bar.getX() - 1.0f, bar.getY() - 2.0f, barWidth + 2.0f, 4.0f);
        g.setColour (isSelected (note) ? colour::textPrimary : colourForChannel);
        g.fillEllipse (bar.getX(), bar.getY() - 1.5f, barWidth, 3.0f);
    }
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    using namespace tokens;

    g.fillAll (colour::background);

    paintNotes (g);
    paintKeyboard (g);
    paintRuler (g);
    paintVelocityLane (g);

    // Corner above the keyboard, where the ruler and the gutter meet. Anchored
    // to the content, not to the component: at y = 0 it painted over the tool
    // strip instead of beside the ruler.
    const auto ruler = rulerArea();

    g.setColour (colour::surface);
    g.fillRect (0, ruler.getY(), size::gutterKeyboard, size::rulerHeight);
    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (ruler.getBottom() - 1, 0.0f, (float) size::gutterKeyboard);

    // The slice line, while it is being drawn. Clipped to the grid so it cannot
    // be mistaken for something that reaches the keyboard or the ruler.
    if (gesture == Gesture::slicing && sliceStart != sliceEnd)
    {
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (noteArea());

        g.setColour (colour::danger);
        g.drawLine ((float) sliceStart.x, (float) sliceStart.y, (float) sliceEnd.x,
                    (float) sliceEnd.y, stroke::bold);
    }

    if (! ProjectEdits::findChannel (document.getState(), editorState.getSelectedChannelId())
              .isValid())
    {
        paint::emptyState (g, noteArea(), "Select a channel in the Channel Rack");
    }
}

} // namespace dew
