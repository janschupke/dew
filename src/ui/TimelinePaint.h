#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/TimelineView.h"

namespace dew::timelinePaint
{

/** The marks every timeline in dew draws, drawn once.

    Four components paint a playhead - the ruler, the step grid, the piano roll
    and the playlist - and three of them paint a bar-and-beat grid. They were
    written one after another, so the two grids disagreed about when a step
    line is too fine to be worth drawing, and the four playheads disagreed
    about the width of the line, the size of the head and whether a stopped
    transport is dimmed or hidden. None of that is a decision any single
    component should be making: a playhead that looks different in two editors
    is two playheads.

    Everything here takes coordinates in the caller's space, so a component
    with a gutter adds its own origin and this stays a painter rather than a
    layout.
*/

/** Bar lines, beat lines and step lines, in that order of weight.

    The sub-beat cutoff is the piano roll's - lines vanish below 6px per step -
    and it is the only one that was thought about: the step grid drew every
    step at full weight, so zooming out turned its background into a solid
    block of divider colour.

    @param steps      the range to walk, from TimelineView::visibleStepRange
    @param originX    the caller's content origin (a keyboard or header gutter)
    @param rightEdge  stop here; a component may be narrower than its range
*/
void verticalGrid (juce::Graphics&, const TimelineView&, juce::Range<int> steps,
                   int stepsPerBar, int stepsPerBeat,
                   float originX, juce::Range<float> y, float rightEdge);

/** The column of the step the transport is inside. Drawn under everything, and
    only while playing: a stopped transport highlighting a step reads as a
    selection. */
void playheadColumn (juce::Graphics&, juce::Rectangle<float>);

/** The line itself. Dimmed while stopped rather than hidden - hiding it made
    "reset the position" look identical to "lose the position", and left
    nothing for a click on the ruler to move. */
void playheadLine (juce::Graphics&, float x, juce::Range<float> y, bool playing);

/** The grab handle on a ruler: a triangle whose point sits on the baseline. */
void playheadHead (juce::Graphics&, float x, float baselineY, bool playing);

/** How far either side of the head a click still counts, and how far outside a
    view the head is still worth drawing. */
inline constexpr float playheadHeadHalfWidth = 5.0f;
inline constexpr float playheadHeadHeight    = 8.0f;

} // namespace dew::timelinePaint
