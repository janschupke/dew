#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "TimelineView.h"

namespace dew
{

/** The FL-style step grid: one row per channel, one column per step.

    A lit cell is a note in the pattern at the channel's base pitch. It is the
    same note data the piano roll edits - the grid is a view, not a second
    representation - so a melodic note written in the piano roll shows up here as
    a lit step, and clearing it here removes that note.

    The grid deliberately occupies the WHOLE panel and paints the region below
    the last channel as inert. Previously it was sized to its rows, leaving most
    of the tab as undifferentiated background where clicks silently did nothing,
    which is what "channel rack clicks don't go through" turned out to mean.
*/
class StepGridComponent : public juce::Component,
                          private juce::Timer,
                          private juce::ScrollBar::Listener
{
public:
    StepGridComponent (ProjectDocument&, AudioEngine&, EditorState&);
    ~StepGridComponent() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

    /** Number of channel rows currently drawn. */
    int getNumRows() const;

    /** Height the rows occupy; anything below this is inert. */
    int getRowsHeight() const;

    const TimelineView& getTimeline() const noexcept { return timeline; }
    bool isScrollable() const;

private:
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::ValueTree currentPattern() const;
    int numSteps() const;

    /** Sets the zoom so the pattern fills the width, within limits: below the
        minimum a step is too small to hit, above the maximum a short pattern
        turns into four enormous blocks. Outside those, the grid scrolls.
    */
    void updateZoom();
    int stepAtX (int x) const;
    int rowAtY (int y) const;
    juce::ValueTree channelForRow (int row) const;

    void applyPaint (const juce::MouseEvent&);
    void repaintCell (juce::Point<int> cell);

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    // A drag paints a run of steps to the same state as the first cell, rather
    // than toggling each one under the cursor - dragging over a lit step would
    // otherwise switch it off again.
    bool dragPaintsOn = true;

    // Set from the press modifiers and held for the whole gesture. Nothing
    // recorded them before, which is why right-click behaved exactly like left.
    bool dragErasing = false;
    bool dragging = false;
    int lastPaintedStep = -1;
    int lastPaintedRow = -1;

    TimelineView timeline;
    juce::ScrollBar horizontalScroll { false };
    bool updatingScrollBar = false;

    static constexpr float minCellWidth = 18.0f;
    static constexpr float maxCellWidth = 64.0f;
    static constexpr int scrollThickness = 10;

    juce::Point<int> hoverCell { -1, -1 };
    int lastPlayheadStep = -1;
    int lastLayoutSteps = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGridComponent)
};

} // namespace dew
