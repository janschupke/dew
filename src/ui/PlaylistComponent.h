#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"

namespace dew
{

/** The arrangement: pattern clips placed on tracks along a bar timeline.

    Click empty space to place the current pattern, drag a clip to move it, drag
    its right edge to lengthen it (which repeats the pattern, as FL does), and
    right-click or alt-click to remove it.
*/
class PlaylistComponent : public juce::Component,
                          private juce::Timer,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener
{
public:
    PlaylistComponent (ProjectDocument&, AudioEngine&, EditorState&);
    ~PlaylistComponent() override;

    void paint (juce::Graphics&) override;

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

    juce::ValueTree playlist() const;
    int numBars() const;
    float barWidth() const;
    int barAtX (int x) const;
    int trackAtY (int y) const;
    juce::ValueTree trackAt (int index) const;
    juce::Rectangle<float> boundsForClip (const juce::ValueTree& clip, int trackIndex) const;
    bool isOnRightEdge (const juce::ValueTree& clip, int trackIndex, juce::Point<int>) const;

    static constexpr int rowHeight = 34;
    static constexpr int headerWidth = 140;
    static constexpr int rulerHeight = 22;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    juce::ValueTree draggedClip;
    juce::ValueTree draggedClipTrack;
    Gesture gesture = Gesture::none;
    int dragBarOffset = 0;
    int lastPlayheadBar = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};

} // namespace dew
