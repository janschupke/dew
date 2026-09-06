#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "model/NoteTools.h"
#include "ui/EditorTools.h"
#include "ui/VerticalZoomButtons.h"
#include "ui/ToolbarOverflow.h"
#include "ui/ZoomButtons.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

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

    EditorTool getTool() const noexcept
    {
        return tools.getTool();
    }
    void setTool (EditorTool wanted, juce::NotificationType notification = juce::sendNotification)
    {
        tools.setTool (wanted, notification);
    }

    /** Selects the tool a view command names, and answers whether it was one
        this strip has - see ToolStrip::applyCommand. */
    bool applyToolCommand (hotkeys::ViewCommand command)
    {
        return tools.applyCommand (command);
    }

    SnapDivision getSnap() const noexcept
    {
        return snap;
    }
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
    /** The project's grid, so the snap dropdown can say which divisions it can
        actually express. `beatUnit` names them and `stepsPerBeat` decides which
        of them fall on whole steps - see NoteTools::fitsGrid. */
    void setGrid (int stepsPerBeat, int beatsPerBar, int beatUnit);
    int getBeatUnit() const noexcept
    {
        return beatUnit;
    }

    std::function<void()> onToolChanged;
    std::function<void()> onSnapChanged;
    std::function<void (int)> onChannelChanged; ///< channel id
    std::function<void (double)> onZoom;        ///< factor, or 0 to fit
    std::function<void (double)> onRowHeight;   ///< the other axis; same shape
    std::function<void (int)> onTranspose;      ///< semitones, positive for up
    std::function<void()> onQuantize;
    std::function<void()> onRandomize;

    /** What the strip needs to show everything, in pixels.

        Declared so the strip can tell whether it has to reserve room for the
        overflow button BEFORE it starts placing, which it cannot work out on
        the way through. Also the honest answer to "how narrow can this window
        get" - about 870, which the 900px resize floor only just clears, and
        which UI scale takes it below: the scale multiplies the PEER, so at
        1.75x the logical window is what the display leaves.
    */
    int preferredWidth() const;

    /** The menu the >> button opens, and what a choice from it does. A seam,
        because showMenuAsync cannot run headlessly - see MenuSeam.h. */
    juce::PopupMenu buildOverflowMenu() const
    {
        return overflow.buildMenu();
    }

    /** The same menu, named the way the other test seams are. */
    juce::PopupMenu getOverflowMenu() const
    {
        return buildOverflowMenu();
    }
    void applyOverflowChoice (int choice)
    {
        overflow.applyMenuChoice (choice);
    }

private:
    void rebuildSnapBox();

    SnapDivision snap = SnapDivision::sixteenth;
    /** ZERO until setGrid has been called, deliberately.

        Initialised to a real metre they would have matched the default project
        exactly, so setGrid's "nothing changed" guard would take the first call
        and the snap would never be clamped against a grid it does not fit -
        which is how the strip opened showing a sixteenth in a project whose
        finest expressible division is an eighth.
    */
    int beatUnit = 0;
    int stepsPerBeat = 0;
    int beatsPerBar = 0;

    ToolStrip tools {
        *this,
        { { EditorTool::select, icons::pointer(), tr (StringId::pianoRoll_toolSelect_help) },
          { EditorTool::paint, icons::pencil(), tr (StringId::pianoRoll_toolPaint_help) },
          { EditorTool::slice, icons::scissors(), tr (StringId::pianoRoll_toolSlice_help) } }
    };

    /** The two dropdowns in this strip said nothing about what they were: one
        showed a channel name and the other a fraction, and neither is
        self-describing at a glance. The status bar explains them on hover; a
        word beside them means you do not have to hover to find out. */
    DewLabel channelCaption, snapCaption;

    DewDropdown snapBox;

    /** Which channel the roll is editing. The roll used to be a passive
        consumer of a selection made in the channel rack, so its own empty state
        could tell you to go and pick a channel but offered no way to do it.
    */
    DewDropdown channelBox;

    ZoomButtons zoomButtons { tr (StringId::pianoRoll_zoomFit_help) };

    VerticalZoomButtons rowHeightButtons { tr (StringId::pianoRoll_rowsShorter_help),
                                           tr (StringId::pianoRoll_rowsTaller_help),
                                           tr (StringId::pianoRoll_rowsFit_help) };

    DewIconButton quantizeButton { icons::quantize(), tr (StringId::pianoRoll_quantize_help) };
    DewIconButton randomizeButton { icons::dice(), tr (StringId::pianoRoll_randomize_help) };

    /** Opened when the strip could not fit everything, and hidden otherwise.
        Its rows are the controls that were dropped, labelled with their own
        tooltips - see ToolbarOverflow. */
    DewIconButton overflowButton { icons::more(), tr (StringId::toolbar_overflow_help) };
    ToolbarOverflow overflow;

    DewIconButton upButton { icons::chevronUp(), tr (StringId::pianoRoll_semitoneUp_help) };
    DewIconButton downButton { icons::chevronDown(), tr (StringId::pianoRoll_semitoneDown_help) };
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
