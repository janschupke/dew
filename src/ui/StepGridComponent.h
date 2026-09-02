#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"

namespace dew
{

/** The FL-style step grid: one row per channel, one column per step.

    A lit cell is a note in the pattern at the channel's base pitch. It is the
    same note data the piano roll edits - the grid is a view, not a second
    representation - so a melodic note written in the piano roll shows up here
    as a lit step, and clearing it here removes that note.
*/
class StepGridComponent : public juce::Component,
                          private juce::Timer
{
public:
    StepGridComponent (ProjectDocument&, AudioEngine&, EditorState&);
    ~StepGridComponent() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    /** Height of one channel row, so the rack can line its headers up. */
    static constexpr int rowHeight = 26;

    int getRequiredHeight() const;

private:
    void timerCallback() override;

    juce::ValueTree currentPattern() const;
    int stepAtX (int x) const;
    int rowAtY (int y) const;
    int numSteps() const;
    float stepWidth() const;

    void applyPaint (const juce::MouseEvent&);

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    // A drag paints a run of steps to the same state as the first cell, rather
    // than toggling each one under the cursor - dragging over a lit step would
    // otherwise switch it off again.
    bool dragPaintsOn = true;
    int lastPaintedStep = -1;
    int lastPaintedRow = -1;

    double lastPlayheadStep = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGridComponent)
};

} // namespace dew
