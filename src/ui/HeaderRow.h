#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MenuSeam.h"
#include "ui/primitives/ButtonBehaviour.h"
#include "ui/primitives/HoverTracker.h"

namespace dew
{

/** The row at the left of a timeline: a channel in the rack, a track in the
    playlist.

    A base class rather than a merge. The two headers hold different controls,
    call different callbacks and word their menus differently, and pretending
    otherwise would produce one class with a flag in it. What they share is how
    a header BEHAVES, and that was written twice:

      - right-click opens a menu anchored at the POINTER rather than at the
        component, because a row is not a button and a menu covering the row
        you just aimed at is worse than one beside the cursor
      - the menu's look and feel has to be set explicitly or DewLookAndFeel's
        popup overrides do not apply
      - the callback holds a SafePointer, because a menu outlives a rebuild
      - double-clicking the name edits it
      - hover is tracked

    The menu itself is built and applied by named methods rather than by a
    lambda inside showMenuAsync, because showMenuAsync cannot be driven
    headlessly and every other gesture here is tested that way. The menu is
    only how a person reaches these; a test reaches them directly.
*/
class HeaderRow : public juce::Component
{
public:
    /** What this header's menu offers. Public because the test seam reads it. */
    virtual juce::PopupMenu buildMenu() const = 0;

    /** Runs the choice the menu returned. Public for the same reason. */
    virtual void applyMenuChoice (int choice) = 0;

protected:
    /** A press this row handles ITSELF, before anything below happens.

        Returning true stops the press selecting the row and stops it opening
        the menu. One row needs it: a playlist header's bottom edge is a resize
        grip, and a grab there is a drag rather than a click on the track.
    */
    virtual bool consumePress (const juce::MouseEvent&)
    {
        return false;
    }

    /** Called on every press, before the menu opens. The rack selects its row
        here, so a menu always acts on the row that was clicked rather than on
        whatever happened to be selected before it. */
    virtual void headerPressed() {}

    /** The label a double-click puts into edit mode, if there is one. */
    virtual juce::Label* editableLabel() = 0;

    HoverTracker hover { *this };

private:
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (consumePress (event))
            return;

        headerPressed();

        if (! event.mods.isPopupMenu())
            return;

        // The menu is the ROW's. The channel rack forwards its children's mouse
        // events here for hover, so a right-click on a knob or on M opened that
        // control's parameter menu and this one on top of it. See isOwnPress.
        if (! isOwnPress (event, *this))
            return;

        auto menu = buildMenu();

        showMenuAt<HeaderRow> (menu, *this, event,
                               [] (HeaderRow& row, int choice) { row.applyMenuChoice (choice); });
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (auto* label = editableLabel())
            if (label->getBounds().contains (event.getPosition()))
                label->showEditor();
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        hover.enter();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover.exit();
    }
};

} // namespace dew
