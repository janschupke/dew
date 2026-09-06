#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Hotkeys.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

/** Which gesture a plain press on empty canvas means.

    A persistent mode, unlike the drag currently in progress. Modifier gestures
    - alt to erase, mod to copy, right-click for the menu - mean the same thing
    in every tool, so a tool only ever decides what an UNMODIFIED press on empty
    space does.

    One enum for every timeline, rather than a RollTool and a PlaylistTool that
    happened to spell two of the same three things. They were not two ideas: the
    pencil means "lay a run of these down" whether the run is notes or clips,
    and having them apart is why the two strips had drifted into building,
    tracking and repainting their tools with four copies of the same code.
*/
enum class EditorTool
{
    select,
    paint,

    /** Cuts what the drag crosses. Offered by the roll and not by the
        playlist, which is the whole reason ToolStrip answers `offers` - a view
        that does not have a tool has to be able to say so, rather than every
        caller knowing which views have which. */
    slice
};

/** A view's tool buttons: the register, the radio group, and which tool is on.

    The strips owned this between them, byte for byte: the same addTool lambda,
    the same setTool with the same comment about the radio group clearing
    itself, the same updateToolButtons naming its buttons one at a time. A third
    timeline would have copied one of them, which is what StripLayout's own doc
    says happened the last time.

    What stays the VIEW's is which tools it offers and what each one says it
    does - a stroke lays notes in one and clips in the other, and a help string
    that tried to cover both would describe neither.
*/
class ToolStrip
{
public:
    /** One tool a view offers.

        The glyph belongs to the register rather than to the view: a pencil
        means paint everywhere, and two views drawing the same tool differently
        is exactly the drift this exists to end.
    */
    struct Entry
    {
        EditorTool tool;
        juce::Path icon;
        juce::String help;
    };

    /** Builds a button per entry as a child of `owner`, in the order given -
        which is the order they are placed and the order they read in. */
    ToolStrip (juce::Component& owner, std::vector<Entry>);

    EditorTool getTool() const noexcept
    {
        return tool;
    }

    void setTool (EditorTool, juce::NotificationType = juce::sendNotification);

    bool offers (EditorTool) const noexcept;

    /** Selects the tool a view command names.

        Answers false for a command that is not a tool at all, and for a tool
        this view does not offer - so the slice key in the playlist does nothing
        rather than doing something unrelated, and the view's switch can pass it
        on to whatever else might want it.
    */
    bool applyCommand (hotkeys::ViewCommand);

    /** Places every button in declaration order, through the view's own
        placer - so a button that does not fit lands in that view's overflow
        menu rather than in one this could not know about. */
    void placeAll (const std::function<void (juce::Component&, int)>&) const;

    /** What the buttons and the steps between them need, for the strip's own
        preferredWidth. Derived rather than a number kept in step by hand: the
        hand-written sums in both strips were what went quietly wrong the moment
        a button was added. */
    int preferredWidth() const noexcept;

    std::function<void()> onToolChanged;

private:
    void updateButtons();

    EditorTool tool = EditorTool::select;

    std::vector<Entry> entries;
    std::vector<std::unique_ptr<DewIconButton>> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolStrip)
};

} // namespace dew
