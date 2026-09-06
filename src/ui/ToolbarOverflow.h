#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MenuSeam.h"
#include "ui/primitives/ButtonBehaviour.h"

namespace dew
{

/** What a strip could not fit, offered as a menu instead of hidden.

    StripLayout used to hide a control there was no room for and say nothing,
    and its own comment conceded that only setResizeLimits was keeping the
    transport bar out of that case. The editor toolbars were not so lucky: UI
    scale multiplies the PEER, so at 1.75x on a laptop the LOGICAL window is
    what the display leaves rather than what anybody chose, and the piano roll's
    toolbar - which wants about 870px - was quietly dropping its zoom, its row
    height, its octave pair and its quantize group.

    The labels come from the CONTROLS, not from a second list. Every one of them
    already carries a tooltip saying what it is, so a menu built from tooltips
    cannot drift from the strip and needs no new strings anywhere.

    Three shapes, because a strip holds three kinds of thing. A button, which
    the menu row triggers. A dropdown, whose own items become a submenu so the
    value can still be changed from in here. And a GROUP - the zoom trio, the
    row-height trio - which is one thing to a strip and three rows to a person,
    so its buttons are offered individually. Without that last case the two
    widest things on the piano roll's toolbar were also the two that vanished
    with nothing to show for them.

    A caption is not offered: it names the control beside it and means nothing
    alone, so the strip drops the pair together instead.
*/
class ToolbarOverflow
{
public:
    void clear()
    {
        items.clear();
    }

    /** Offer this control from the menu if the strip could not place it. */
    void add (juce::Component& c)
    {
        items.push_back (&c);
    }

    bool isEmpty() const noexcept
    {
        return items.empty();
    }

    /** The menu, and the ids it hands back.

        A control's index is (position + 1) * idsPerControl, so a dropdown's own
        items can occupy the range just above it. That keeps one flat id space
        without a second table saying which id meant what.
    */
    static constexpr int idsPerControl = 100;

    juce::PopupMenu buildMenu() const
    {
        juce::PopupMenu menu;

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto base = (int) (i + 1) * idsPerControl;

            if (auto* box = dynamic_cast<juce::ComboBox*> (items[i]))
            {
                juce::PopupMenu values;

                for (int n = 0; n < box->getNumItems(); ++n)
                    values.addItem (base + n + 1, box->getItemText (n), true,
                                    box->getSelectedItemIndex() == n);

                menu.addSubMenu (labelFor (*items[i]), values);
                continue;
            }

            if (auto* button = dynamic_cast<juce::Button*> (items[i]))
            {
                menu.addItem (base, labelFor (*items[i]), button->isEnabled(),
                              button->getToggleState());
                continue;
            }

            // A group: its buttons, one row each, in the order it holds them.
            auto n = 0;

            for (auto* child : buttonsIn (*items[i]))
                menu.addItem (base + ++n, labelFor (*child), child->isEnabled(),
                              child->getToggleState());
        }

        return menu;
    }

    /** Does what the chosen row says. Silent for 0, which is what a dismissed
        menu returns. */
    void applyMenuChoice (int choice) const
    {
        if (choice <= 0)
            return;

        const auto index = (size_t) (choice / idsPerControl) - 1;

        if (index >= items.size())
            return;

        if (auto* box = dynamic_cast<juce::ComboBox*> (items[index]))
        {
            const auto n = choice % idsPerControl;

            if (n > 0)
                box->setSelectedItemIndex (n - 1, juce::sendNotification);

            return;
        }

        if (auto* button = dynamic_cast<juce::Button*> (items[index]))
        {
            button->triggerClick();
            return;
        }

        const auto children = buttonsIn (*items[index]);
        const auto n = choice % idsPerControl;

        if (n > 0 && (size_t) n <= children.size())
            children[(size_t) n - 1]->triggerClick();
    }

private:
    /** The buttons a group holds, in its own order. One level: a group is a row
        of controls, not a tree, and a deeper walk would offer the internals of
        anything that happened to be built from smaller parts. */
    static std::vector<juce::Button*> buttonsIn (juce::Component& group)
    {
        std::vector<juce::Button*> found;

        for (auto* child : group.getChildren())
            if (auto* button = dynamic_cast<juce::Button*> (child))
                found.push_back (button);

        return found;
    }

    /** What the control calls itself. A tooltip is the sentence dew already
        shows for it in the status strip, so this is the same words in a second
        place rather than a second set of words. */
    static juce::String labelFor (juce::Component& c)
    {
        if (auto* client = dynamic_cast<juce::TooltipClient*> (&c))
            if (const auto tip = client->getTooltip(); tip.isNotEmpty())
                return tip;

        return c.getName();
    }

    std::vector<juce::Component*> items;
};

} // namespace dew
