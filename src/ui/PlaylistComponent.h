#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/ProjectDocument.h"
#include "ui/EditorState.h"
#include "model/AutomationTargets.h"
#include "ui/PlaylistToolbar.h"
#include "ui/TimelineRuler.h"
#include "ui/TimelineView.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

class SamplePool;

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
    /** The sample pool is passed in rather than reached through the engine.

        A file cache is not the engine's to lend out - the engine only needs
        somewhere to get samples from, which is what SampleProvider is for. The
        UI wants the peaks too, so it takes the pool itself, the way
        InstrumentPanel and SampleSection already do.
    */
    PlaylistComponent (ProjectDocument&, AudioEngine&, EditorState&, SamplePool* = nullptr);
    ~PlaylistComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

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

    /** The ruler strip, in this component's coordinates, so a test can aim at
        it rather than recomputing the layout and drifting from it - the piano
        roll already publishes its areas for the same reason.
    */
    juce::Rectangle<int> getRulerArea() const
    {
        return { tokens::size::gutterTrack, rulerTop(),
                 (int) contentWidth(), tokens::size::rulerHeight };
    }

    /** The tool strip, so a test can drive the tools and the zoom buttons
        through the same seam the user reaches them by.
    */
    PlaylistToolbar& getToolbar() noexcept { return toolbar; }

    PlaylistTool getTool() const noexcept { return toolbar.getTool(); }
    void setTool (PlaylistTool tool) { toolbar.setTool (tool); }

    /** Frames the whole song: all of it visible, scrolled to the start. */
    void zoomToFit();

    void addTrack();
    void removeTrack (juce::ValueTree track);

    /** Drives a track header's context-menu item without opening the menu.

        juce::PopupMenu::showMenuAsync cannot run headlessly, so the menu is the
        only part of this a test cannot reach; everything it does is behind here.
        Returns false if there is no such track.
    */
    bool applyTrackMenuChoice (int trackIndex, int choice);

    /** The items a track header's menu offers, for a test to assert against. */
    juce::StringArray trackMenuItems (int trackIndex) const;

    /** The same, for the menu a clip's right-click opens. */
    juce::StringArray clipMenuItems (int trackIndex, int bar) const;
    bool applyClipMenuChoice (int trackIndex, int bar, int choice);

    /** Where an automation point sits, for tests and for hit-testing. */
    juce::Point<float> pointPosition (const juce::ValueTree& clip, int trackIndex,
                                      const juce::ValueTree& point) const;

private:
    class TrackHeader;

    // Scrubbing and range-selecting are not here: the ruler's whole gesture
    // lives in ruler::Gesture, shared with the piano roll and the channel rack.
    enum class Gesture { none, moving, resizing, draggingPoint, painting };

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

    /** The top of the ruler, and the top of the lanes below it. Every other
        vertical measurement is taken from these rather than from the
        component's own top, so the toolbar's height is stated once and cannot
        leave one part of the layout behind when it changes.
    */
    static constexpr int rulerTop() { return tokens::size::stripToolbar; }
    static constexpr int lanesTop() { return tokens::size::stripToolbar + tokens::size::rulerHeight; }

    /** Lays a clip of the current pattern in the cell under this point, unless
        one is already there. Returns true if it wrote one, so a stroke can tell
        whether it has done anything worth repainting.
    */
    bool paintClipAt (juce::Point<int> position);

    /** Copies a clip rather than moving it: a mod-drag leaves the original
        where it was, and a mod-shift-drag gives the copy a pattern of its own.
    */
    void beginCopyDrag (int targetTrackIndex, int targetBar);
    float playheadX() const;
    /** Re-clamps the scroll and re-ranges the scrollbar. This used to also
        recompute the zoom from the song's length, on every resize - which is
        why the playlist could not be zoomed at all: any change re-fitted it.
    */
    void updateScrollBar();
    void zoomBy (double factor, float anchorX);
    void rebuildHeaders();
    void openPatternOf (const juce::ValueTree& clip);
    void showAutomationMenu();

    /** The menu a right-click on a lane opens, and what its items do.

        What is offered depends on what is under the pointer: a curve point, a
        clip, or empty space. Built and applied separately so a test can drive
        the actions without a menu that cannot open headlessly.
    */
    juce::PopupMenu buildClipMenu (const juce::ValueTree& track, int bar) const;
    void applyClipChoice (juce::ValueTree track, int bar, int choice);

    /** Set while a clip menu is open, so its items act on the point that was
        actually under the pointer rather than re-hit-testing from a bar.
    */
    juce::ValueTree menuPoint;

    juce::ValueTree automationOf (const juce::ValueTree& clip) const;

    /** The automation point under this position, within grabbing distance. */
    juce::ValueTree pointAt (const juce::ValueTree& clip, int trackIndex, juce::Point<int>) const;

    /** Turns a position inside an automation clip into a step and a 0..1 value. */
    void positionToCurve (const juce::ValueTree& clip, int trackIndex, juce::Point<int>,
                          double& step, double& value) const;

    /** An audio clip: its channel's waveform, in its channel's colour. */
    void paintAudioClip (juce::Graphics&, const juce::ValueTree& clip,
                         juce::Rectangle<float> bounds, bool audible);

    void paintAutomationClip (juce::Graphics&, const juce::ValueTree& clip, int trackIndex,
                              juce::Rectangle<float> bounds, bool audible);


    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;
    SamplePool* samplePool = nullptr;

    PlaylistToolbar toolbar;

    TimelineView timeline;

    /** Scrub, span-select and clear. This timeline counts BARS, so the gesture
        works in bars here and in steps everywhere else - it never converts, so
        it cannot get the conversion wrong.
    */
    ruler::Gesture rulerGesture;

    juce::ScrollBar horizontalScroll { false };
    bool updatingScrollBar = false;

    juce::OwnedArray<TrackHeader> headers;
    DewButton addAutomationButton { "+ Automation", DewButton::Role::ghost };

    // In the header column below the last track: the next empty row of the list,
    // where the track it adds will appear.
    DewButton addTrackButton { "+ Track", DewButton::Role::ghost };

    static constexpr float pointGrabRadius = 7.0f;

    juce::ValueTree draggedClip;
    juce::ValueTree draggedClipTrack;
    juce::ValueTree draggedPoint;
    Gesture gesture = Gesture::none;
    int dragBarOffset = 0;
    int dropTrackIndex = -1;

    /** A mod-drag copies rather than moves, and mod-shift gives the copy its
        own pattern. Latched at the press and cleared by the copy itself, so a
        drag makes ONE copy however far it then travels.
    */
    bool dragCopies = false;
    bool dragCopyIsUnique = false;

    /** The cell the paint stroke last wrote into, so dragging within one cell
        does not try to lay the same clip over and over.
    */
    juce::Point<int> lastPaintedCell { -1, -1 };

    /** Whether the view belongs to the user yet.

        Until it does, a layout re-fits the song to the window - the playlist
        should open showing the whole arrangement, and the FIRST layout is no
        good for deciding that because it can happen before the window has its
        real size. From the moment someone zooms or scrolls, the view is theirs
        and a layout leaves it alone. Re-fitting on every layout, which is what
        this did, is why the playlist could not be zoomed at all.
    */
    bool viewIsUsers = false;

    // The playhead is repainted on its own strip at 60Hz; repainting the whole
    // arrangement that often is what made the line jump a bar at a time.
    float lastPaintedPlayheadX = -1.0f;
    bool lastPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};

} // namespace dew
