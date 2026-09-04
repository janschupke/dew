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

} // namespace dew::dialog
