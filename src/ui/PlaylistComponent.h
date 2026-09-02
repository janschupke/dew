#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "../model/AutomationTargets.h"
#include "TimelineView.h"
#include "primitives/DewControls.h"

namespace dew
{

/** The arrangement: pattern clips placed on tracks along a bar timeline.

    Click empty space to place the current pattern, drag a clip to move it -
    including onto another track - drag its right edge to lengthen it (which
    repeats the pattern, as FL does), right-click or alt-click to remove it, and
    double-click it to open its pattern in the piano roll.

    The timeline is in bars rather than steps, but it is the same TimelineView
    the note editors use, so scrolling and zooming behave identically.
*/
class PlaylistComponent : public juce::Component,
                          private juce::Timer,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    PlaylistComponent (ProjectDocument&, AudioEngine&, EditorState&);
    ~PlaylistComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void refresh();

    /** Creates an automation for a curated target and places a clip for it, so
        choosing a target produces something visible rather than an entry in a
        list nobody can see.
    */
    juce::ValueTree createAutomationClip (const AutomationTarget&, int startBar, int lengthBars);

    /** Called when a clip is double-clicked, after its pattern has been made
        current. The playlist does not know about tabs; whoever owns them does.
    */
    std::function<void()> onOpenPatternInPianoRoll;

    // --- geometry, so a test can aim at a clip instead of recomputing layout --
    juce::Rectangle<float> getBoundsForClip (const juce::ValueTree& clip, int trackIndex) const
    {
        return boundsForClip (clip, trackIndex);
    }

    const TimelineView& getTimeline() const noexcept { return timeline; }
    int getNumTracks() const;

    /** Where an automation point sits, for tests and for hit-testing. */
    juce::Point<float> pointPosition (const juce::ValueTree& clip, int trackIndex,
                                      const juce::ValueTree& point) const;

private:
    class TrackHeader;

    enum class Gesture { none, moving, resizing, draggingPoint };

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::ValueTree playlist() const;
    int numBars() const;
    int barAtX (int x) const;
    int trackAtY (int y) const;
    juce::ValueTree trackAt (int index) const;
    juce::Rectangle<float> boundsForClip (const juce::ValueTree& clip, int trackIndex) const;
    bool isOnRightEdge (const juce::ValueTree& clip, int trackIndex, juce::Point<int>) const;

    int tracksBottom() const;
    float contentWidth() const;
    float playheadX() const;
    void updateZoom();
    void rebuildHeaders();
    void openPatternOf (const juce::ValueTree& clip);
    void showAutomationMenu();

    juce::ValueTree automationOf (const juce::ValueTree& clip) const;

    /** The automation point under this position, within grabbing distance. */
    juce::ValueTree pointAt (const juce::ValueTree& clip, int trackIndex, juce::Point<int>) const;

    /** Turns a position inside an automation clip into a step and a 0..1 value. */
    void positionToCurve (const juce::ValueTree& clip, int trackIndex, juce::Point<int>,
                          double& step, double& value) const;

    void paintAutomationClip (juce::Graphics&, const juce::ValueTree& clip, int trackIndex,
                              juce::Rectangle<float> bounds, bool audible);

    static constexpr int rowHeight = 34;
    static constexpr int headerWidth = 156;
    static constexpr int rulerHeight = 22;
    static constexpr int scrollThickness = 10;
    static constexpr float minBarWidth = 26.0f;
    static constexpr float maxBarWidth = 160.0f;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    TimelineView timeline;
    juce::ScrollBar horizontalScroll { false };
    bool updatingScrollBar = false;

    juce::OwnedArray<TrackHeader> headers;
    DewButton addAutomationButton { "+ Automation", DewButton::Role::ghost };

    static constexpr float pointGrabRadius = 7.0f;

    juce::ValueTree draggedClip;
    juce::ValueTree draggedClipTrack;
    juce::ValueTree draggedPoint;
    Gesture gesture = Gesture::none;
    int dragBarOffset = 0;
    int dropTrackIndex = -1;

    // The playhead is repainted on its own strip at 60Hz; repainting the whole
    // arrangement that often is what made the line jump a bar at a time.
    float lastPaintedPlayheadX = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};

} // namespace dew
