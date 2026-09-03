#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/VerticalZoomButtons.h"
#include "ui/ZoomButtons.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** Which gesture a plain press on an empty cell of the arrangement means.

    A persistent mode, the way RollTool is. Modifier gestures - alt to delete,
    mod to copy, right-click for the menu - mean the same thing in either tool,
    so the tool only ever decides what an unmodified press on empty space does.
*/
enum class PlaylistTool
{
    select,
    paint
};

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

    PlaylistTool getTool() const noexcept { return tool; }
    void setTool (PlaylistTool, juce::NotificationType = juce::sendNotification);

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


private:
    void updateToolButtons();

    PlaylistTool tool = PlaylistTool::select;

    DewIconButton selectButton { icons::pointer(), "Select tool (1)" };
    DewIconButton paintButton { icons::pencil(), "Paint tool - drag to lay a run of clips (2)" };

    ZoomButtons zoomButtons { "Fit the song to the window (0)" };
    VerticalZoomButtons heightButtons { "Shorter tracks (alt--)", "Taller tracks (alt-+)",
                                        "Fit the tracks to the window" };

    /** Where a vertical rule goes between groups. Recorded during layout and
        painted afterwards, the way the piano roll's strip does it, so the two
        cannot disagree about where a group ends.
    */
    juce::Array<int> groupDividers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistToolbar)
};

} // namespace dew
