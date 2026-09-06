#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

#include "ui/EditorTools.h"
#include "ui/VerticalZoomButtons.h"
#include "ui/ToolbarOverflow.h"
#include "ui/ZoomButtons.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

/** The playlist's tool strip: tools and zoom.

    The playlist had neither. Its zoom was a formula - the song's length divided
    into the window on every resized() - so it could not be zoomed at all, and
    an arrangement longer than a few bars had no reading other than the one the
    window happened to give it. Placing clips was one at a time.

    Built out of the same primitives as PianoRollToolbar and reporting through
    callbacks for the same reason: so it can be laid out and driven in a test
    with no document or engine behind it.
*/
class PlaylistToolbar : public juce::Component
{
public:
    PlaylistToolbar();

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

    std::function<void()> onToolChanged;

    /** A zoom factor, or 0 to fit the song to the window. The same shape the
        piano roll's strip reports, so the two mean one thing.
    */
    std::function<void (double)> onZoom;

    /** The same, for the height of every lane: a factor, or 0 to fit the tracks
        to the window. Deliberately the shape onZoom reports - one strip holding
        both groups states one idea on two axes rather than two ideas once.
    */
    std::function<void (double)> onTrackHeight;

    /** What the strip needs to show everything - see PianoRollToolbar's. */
    int preferredWidth() const;

    /** The menu the >> button opens, and what a choice from it does. A seam:
        showMenuAsync cannot run headlessly - see MenuSeam.h. */
    juce::PopupMenu getOverflowMenu() const
    {
        return overflow.buildMenu();
    }
    void applyOverflowChoice (int choice)
    {
        overflow.applyMenuChoice (choice);
    }

private:
    /** Shown only when the strip has run out of room - see ToolbarOverflow. */
    DewIconButton overflowButton { icons::more(), tr (StringId::toolbar_overflow_help) };
    ToolbarOverflow overflow;

    /** Select and paint, and deliberately not slice: a clip is not cut by
        dragging across it in this tree, and a tool that did nothing would be
        worse than an absent one. ToolStrip::offers is what lets the key say so.
    */
    ToolStrip tools {
        *this,
        { { EditorTool::select, icons::pointer(), tr (StringId::playlist_toolSelect_help) },
          { EditorTool::paint, icons::pencil(), tr (StringId::playlist_toolPaint_help) } }
    };

    ZoomButtons zoomButtons { tr (StringId::playlist_zoomFit_help) };
    VerticalZoomButtons heightButtons { tr (StringId::playlist_tracksShorter_help),
                                        tr (StringId::playlist_tracksTaller_help),
                                        tr (StringId::playlist_tracksFit_help) };

    /** Where a vertical rule goes between groups. Recorded during layout and
        painted afterwards, the way the piano roll's strip does it, so the two
        cannot disagree about where a group ends.
    */
    juce::Array<int> groupDividers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistToolbar)
};

} // namespace dew
