#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "ui/MainComponent.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

namespace dew::testing
{

/** Walking every control in the window, which three gates need and each of
    which had written its own.

    The gates are "every control says what it is" (HoverHelpTests), "every
    control a screen reader meets has a name" and "every control can be reached
    by the keyboard" (AccessibilityTests). They differ only in what they ask of
    a control once they have found it, and finding it is the part with the traps
    in it.
*/

/** Where a control lives, as the chain of component IDs above it - so a failure
    names the panel to go and look at rather than a count.
*/
inline juce::String describe (juce::Component& c)
{
    juce::StringArray path;

    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (p->getComponentID().isNotEmpty())
            path.insert (0, p->getComponentID());

    if (path.isEmpty())
        path.add ("(no id)");

    return path.joinIntoString (" > ") + " @" + c.getBounds().toString();
}

/** Is this something a person clicks, drags or types into?

    By TYPE rather than by "does it handle the mouse", because the second
    question has no answer from outside a component - and because the list of
    things dew calls a control is exactly the list of primitives it built.
*/
inline bool isAControl (juce::Component& c)
{
    return dynamic_cast<DewButton*> (&c) != nullptr || dynamic_cast<DewIconButton*> (&c) != nullptr
           || dynamic_cast<DewLetterToggle*> (&c) != nullptr
           || dynamic_cast<DewKnob*> (&c) != nullptr
           || dynamic_cast<DewNumberField*> (&c) != nullptr
           || dynamic_cast<juce::ComboBox*> (&c) != nullptr;
}

inline void walk (juce::Component& root, const std::function<void (juce::Component&)>& visit)
{
    for (auto* child : root.getChildren())
    {
        visit (*child);
        walk (*child, visit);
    }
}

/** Every control in EVERY tab, and the count of them.

    The tab loop is the trap: a juce::TabbedComponent parents only the CURRENT
    tab's content, so a walk of a freshly built MainComponent covers the channel
    rack and none of the other four - which is a gate that passes while saying
    almost nothing.

    Returns how many controls were visited, so a caller can assert it found a
    plausible number rather than passing because it found none.
*/
inline int forEachControl (MainComponent& component,
                           const std::function<void (juce::Component&)>& visit)
{
    auto controls = 0;

    for (int tab = 0; tab < Settings::numTabs; ++tab)
    {
        component.showTab (tab);
        component.resized();

        walk (component,
              [&] (juce::Component& c)
              {
                  if (! isAControl (c))
                      return;

                  ++controls;
                  visit (c);
              });
    }

    return controls;
}

} // namespace dew::testing
