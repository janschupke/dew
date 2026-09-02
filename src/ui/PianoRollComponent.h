#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"

namespace dew
{

/** Note editing with pitch: click to add, drag to move, drag the right edge to
    resize, right-click or alt-click to delete.

    Edits the same notes the step grid does. A note written here at a pitch the
    channel does not use still lights that step in the rack, because there is
    one representation rather than two.
*/
class PianoRollComponent : public juce::Component,
                           private juce::Timer,
                           private juce::ValueTree::Listener,
                           private juce::ChangeListener
{
public:
    PianoRollComponent (ProjectDocument&, AudioEngine&, EditorState&);
    ~PianoRollComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void refresh();

private:
    enum class Gesture { none, moving, resizing };

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::ValueTree currentPattern() const;
    juce::ValueTree noteAt (juce::Point<int>) const;

    int numSteps() const;
    float stepWidth() const;
    int stepAtX (int x) const;
    int pitchAtY (int y) const;
    juce::Rectangle<float> boundsForNote (const juce::ValueTree& note) const;
    bool isOnRightEdge (const juce::ValueTree& note, juce::Point<int>) const;

    static constexpr int rowHeight = 12;
    static constexpr int lowestPitch = 24;    ///< C1
    static constexpr int highestPitch = 96;   ///< C7
    static constexpr int numRows = highestPitch - lowestPitch + 1;
    static constexpr int keyboardWidth = 44;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    juce::ValueTree draggedNote;
    Gesture gesture = Gesture::none;
    int dragStepOffset = 0;
    int dragPitchOffset = 0;
    int lastPlayheadStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};

} // namespace dew
