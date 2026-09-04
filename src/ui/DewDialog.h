#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace dew::dialog
{

/** Opens a panel as a dialog, with dew's window conventions applied once.

    Six call sites set the same six fields - background, centring, escape-to-
    close, native title bar, not resizable - and differed only in what they were
    showing and what it was called. The conventions are the point: a dialog that
    resizes, or that ignores escape, is a dialog that behaves unlike the other
    five.

    Takes ownership of `content`, as LaunchOptions::content.setOwned does. The
    dialog is modeless and deletes itself when closed.
*/
void launch (juce::Component* content, const juce::String& title, juce::Component* centreAround);

/** The tallest a dialog's content may be and still fit on this screen.

    dew's dialogs are not resizable, by convention, and the render panel grows
    itself as rows appear. Neither fact is a problem until the interface is
    scaled: at 1.75x a 470-tall panel wants 822 logical pixels of a screen that
    has fewer, and because its buttons are laid out from the BOTTOM they are the
    part that goes off the edge - on a dialog that cannot be resized or moved
    far enough to bring them back.

    So a dialog taller than this scrolls instead, and anything that resizes
    itself clamps to it.
*/
int maxContentHeight();

} // namespace dew::dialog
