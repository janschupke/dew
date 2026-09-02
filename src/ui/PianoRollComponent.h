#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/AudioEngine.h"
#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "TimelineView.h"

namespace dew
{

/** Note editing with pitch, over a scrollable and zoomable timeline.

    Edits the same notes the step grid does. A note written here at a pitch the
    channel does not use still lights that step in the rack, because there is one
    representation rather than two.

    Layout, all four parts driven by the same TimelineView so they cannot drift:

        corner   | ruler                       |
        keyboard | notes                       | v scrollbar
        VEL      | velocity lane               |
                 | h scrollbar                 |

    Gestures:
        click empty          add a note at the last length and velocity used
        drag after adding    set its length in the same gesture
        click a note         select it; drag moves the whole selection
        shift-click a note   add to or remove from the selection
        cmd/ctrl-drag empty  rubber-band select
        right/alt-click      delete
        delete / backspace   delete the selection
        cmd/ctrl-A           select every note on this channel
        drag in the lane     set velocity
        cmd/ctrl-wheel       zoom around the pointer
        shift-wheel          scroll horizontally
*/
class PianoRollComponent : public juce::Component,
                           private juce::Timer,
                           private juce::ValueTree::Listener,
                           private juce::ChangeListener,
                           private juce::ScrollBar::Listener
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

    void refresh();

    /** Frames the pattern: all of it visible, scrolled to its notes. */
    void zoomToFit();

    /** Scrolls the selected channel's notes into view, but only when none of
        them are already visible - re-framing a view you can already work in is
        just the editor moving under your hands.
    */
    void scrollToNotesIfOffscreen();

    /** Scrolls so this pitch sits in the middle of the note area. */
    void centreOnPitch (int pitch);

    // --- geometry, public so a test can aim at a note instead of recomputing
    //     the layout and drifting from it -------------------------------------
    const TimelineView& getTimeline() const noexcept { return timeline; }
    int getNumSelectedNotes() const noexcept         { return selection.size(); }

    /** Where a note is painted, in this component's coordinates. */
    juce::Rectangle<float> getBoundsForNote (const juce::ValueTree& note) const { return boundsForNote (note); }

    juce::Rectangle<int> getNoteArea() const     { return noteArea(); }
    juce::Rectangle<int> getKeyboardArea() const { return keyboardArea(); }
    juce::Rectangle<int> getVelocityArea() const { return velocityArea(); }
    juce::Rectangle<int> getRulerArea() const    { return rulerArea(); }
    juce::Rectangle<float> getVelocityBarBounds (const juce::ValueTree& note) const
    {
        return velocityBarBounds (note);
    }

    /** The pitch currently being auditioned, or -1. */
    int getAuditionPitch() const noexcept { return auditionPitch; }

    /** Where the view is, so a session can be restored to it. */
    void captureView (double& zoom, double& scroll, double& pitchScroll) const;
    void applyView (double zoom, double scroll, double pitchScroll);

private:
    enum class Gesture { none, moving, resizing, selecting, velocity, auditioning, erasing, scrubbing };

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::ValueTree currentPattern() const;
    juce::ValueTree noteAt (juce::Point<int>) const;

    /** Deletes every note the pointer passed over between two drag samples.

        Sampled along the segment rather than at its ends: a drag reports a
        handful of positions per second, so a quick sweep skips whole notes if
        you only look where the pointer happened to be reported.
    */
    void eraseAlong (juce::Point<int> from, juce::Point<int> to);

    /** Moves the transport to the position a ruler x means. */
    void seekToRulerX (int x);
    int numSteps() const;

    // --- geometry ------------------------------------------------------------
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> velocityArea() const;

    float contentWidth() const;
    int stepAtX (int x) const;
    int pitchAtY (int y) const;
    int firstVisiblePitch() const;
    juce::Rectangle<float> boundsForNote (const juce::ValueTree& note) const;

    /** Where a note's velocity bar is drawn in the lane.

        One function for painting and for hit-testing: they disagreed by 8px,
        which is why the bar top was never under the cursor that was supposedly
        dragging it.
    */
    juce::Rectangle<float> velocityBarBounds (const juce::ValueTree& note) const;

    /** The velocity bar under this point, or an invalid tree. */
    juce::ValueTree velocityBarAt (juce::Point<int>) const;

    /** Sounds `pitch` on the selected channel, releasing whatever was sounding.
        A no-op when that pitch is already the one being auditioned.
    */
    void startAudition (int pitch);
    void stopAudition();
    bool isOnRightEdge (const juce::ValueTree& note, juce::Point<int>) const;

    void updateScrollBars();
    void selectOnly (const juce::ValueTree& note);
    bool isSelected (const juce::ValueTree& note) const;
    void deleteSelection();
    void selectAllOnChannel();
    void applyVelocityAt (juce::Point<int>);
    void paintKeyboard (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintVelocityLane (juce::Graphics&);
    juce::Colour channelColour() const;

    /** How finely an erase sweep is sampled between two drag positions. Half a
        row, so nothing that can be drawn can be skipped over.
    */
    static constexpr int eraseStridePx = 6;

    static constexpr int rowHeight       = 14;
    static constexpr int lowestPitch     = 12;   ///< C0
    static constexpr int highestPitch    = 108;  ///< C8
    static constexpr int numRows         = highestPitch - lowestPitch + 1;
    static constexpr int keyboardWidth   = 54;
    static constexpr int rulerHeight     = 22;
    static constexpr int velocityHeight  = 62;
    static constexpr int scrollThickness = 10;

    /** Space above and below a velocity bar. Shared by painting and hit-testing
        so the two cannot drift apart again.
    */
    static constexpr int barPadding = 4;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    TimelineView timeline;
    juce::ScrollBar horizontalScroll { false };
    juce::ScrollBar verticalScroll { true };
    bool updatingScrollBars = false;

    double pitchScrollPx = 0.0;

    juce::Array<juce::ValueTree> selection;
    juce::ValueTree draggedNote;
    Gesture gesture = Gesture::none;
    int dragStepOffset = 0;
    int dragPitchOffset = 0;
    juce::Point<int> dragOrigin;

    // Where the erase sweep last looked. mouseDrag reports discrete positions,
    // so the segment between two of them is what actually has to be swept.
    juce::Point<int> lastErasePosition;
    juce::Rectangle<int> rubberBand;
    juce::Array<juce::ValueTree> selectionAtDragStart;

    // Where each selected note started, so a multi-note move stays rigid rather
    // than every note snapping to the same offset.
    juce::Array<juce::Point<int>> selectionOrigins;

    int lastPlayheadStep = -1;
    bool lastPlaying = false;
    bool didFitOnce = false;

    /** The key currently sounding under the pointer, so it can be lit and
        released. -1 when nothing is being auditioned.
    */
    int auditionPitch = -1;

    juce::ValueTree draggedVelocityNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};

} // namespace dew
