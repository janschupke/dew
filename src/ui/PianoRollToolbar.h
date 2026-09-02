#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/NoteTools.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** Which gesture a plain press on the note area means.

    A persistent mode, unlike PianoRollComponent's Gesture, which is the drag
    currently in progress. Modifier gestures - erase, rubber-band, shift-select -
    mean the same thing in every tool, so the tool only ever decides what an
    unmodified press on empty space does.
*/
enum class RollTool
{
    select,
    paint,
    slice
};

/** The piano roll's tool strip: tools, the snap grid, and the edits that act on
    a selection.

    A feature component rather than a design-system primitive - it is built out
    of DewIconButton, DewButton and a stock ComboBox, and owns no drawing of its
    own beyond the strip's background and its group dividers.

    It reports through callbacks instead of reaching into the roll, so it can be
    laid out and driven in a test without a document or an engine behind it.
*/
class PianoRollToolbar : public juce::Component
{
public:
    PianoRollToolbar();

    void paint (juce::Graphics&) override;
    void resized() override;

    RollTool getTool() const noexcept { return tool; }
    void setTool (RollTool, juce::NotificationType = juce::sendNotification);

    SnapDivision getSnap() const noexcept { return snap; }
    void setSnap (SnapDivision, juce::NotificationType = juce::sendNotification);

    /** The channels the roll can be pointed at, and which one it is on.

        Handed in rather than read from the document: this strip is built out of
        primitives and can be laid out and driven in a test with no project
        behind it, and reaching into the tree here would end that.
    */
    void setChannels (const juce::StringArray& names, const juce::Array<int>& ids);
    void setSelectedChannel (int channelId);
    int getSelectedChannel() const noexcept;

    /** The project's notational denominator, which names the snap divisions.
        Handed in for the same reason the channels are: this strip never reads
        the document. Relabels the box in place, keeping the current selection.
    */
    void setBeatUnit (int);
    int getBeatUnit() const noexcept { return beatUnit; }

    std::function<void()>    onToolChanged;
    std::function<void()>    onSnapChanged;
    std::function<void (int)> onChannelChanged;   ///< channel id
    std::function<void (double)> onZoom;          ///< factor, or 0 to fit
    std::function<void (int)> onTranspose;   ///< semitones, positive for up
    std::function<void()>    onQuantize;
    std::function<void()>    onRandomize;

    static constexpr int preferredHeight = 34;

private:
    void updateToolButtons();

    void rebuildSnapBox();

    RollTool tool = RollTool::select;
    SnapDivision snap = SnapDivision::sixteenth;
    int beatUnit = 4;

    DewIconButton selectButton { icons::pointer(), "Select tool (1)" };
    DewIconButton paintButton { icons::pencil(), "Paint tool - drag to write a run of notes (2)" };
    DewIconButton sliceButton { icons::scissors(), "Slice tool - drag across notes to cut them (3)" };

    juce::ComboBox snapBox;

    /** Which channel the roll is editing. The roll used to be a passive
        consumer of a selection made in the channel rack, so its own empty state
        could tell you to go and pick a channel but offered no way to do it.
    */
    juce::ComboBox channelBox;

    DewIconButton zoomOutButton { icons::zoomOut(), "Zoom out (-)" };
    DewIconButton zoomInButton { icons::zoomIn(), "Zoom in (+)" };
    DewIconButton zoomFitButton { icons::fitToContent(), "Fit the pattern to the window" };

    DewIconButton quantizeButton { icons::quantize(), "Quantize to the snap grid (Q)" };
    DewIconButton randomizeButton { icons::dice(), "Randomize velocity and timing (R)" };

    DewIconButton upButton { icons::chevronUp(), "Up a semitone (Up)" };
    DewIconButton downButton { icons::chevronDown(), "Down a semitone (Down)" };
    DewButton octaveUpButton { "+12", DewButton::Role::normal };
    DewButton octaveDownButton { "-12", DewButton::Role::normal };

    /** Where a vertical rule goes between groups. Recorded during layout and
        painted afterwards, the way the transport bar does it, so the two cannot
        disagree about where a group ends.
    */
    juce::Array<int> groupDividers;

    bool updatingSnapBox = false;
    bool updatingChannelBox = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollToolbar)
};

} // namespace dew
