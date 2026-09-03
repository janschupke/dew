#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/AutomationTargets.h"
#include "app/ProjectDocument.h"

namespace dew::paramMenu
{

/** The menu a right-click on any spec-built control opens.

    Automation used to be reachable from exactly one button, which opened a flat
    picker of every target in the project: you had to know a target's NAME and
    find it in a list, and the knob in front of you had no idea it was
    automatable. This is the other direction - the control asks what it drives,
    and offers a curve for it.

    Numbered explicitly, the way ClipMenuItem is, because a test names an item by
    its number and inserting one in the middle would silently re-aim every one of
    them.
*/
enum class Item
{
    createClip     = 1,
    resetToDefault = 2
};

/** What a control needs to know to offer its own menu.

    The control itself holds none of this: dew_design "knows nothing about a
    project", as its own CMakeLists says, so a control carries a
    std::function<void()> and the editor that built it closes over the rest.
*/
struct Context
{
    ProjectDocument* document = nullptr;

    /** The node the property lives on, asked for FRESH each time. A panel
        re-points at another channel without rebuilding its knobs, so a captured
        ValueTree would go stale and a menu would edit the channel you were
        looking at a moment ago. */
    std::function<juce::ValueTree()> owner;

    /** BY VALUE, not by pointer.

        effectParamsFor returns its table by value, so the spec a panel builds a
        knob from lives in a temporary that dies at the end of the loop - a
        pointer to it would dangle for exactly as long as the control it belongs
        to. A ParamSpec is a handful of numbers plus pointers to static strings
        and a static choice table, so copying one is both cheap and safe.
    */
    ParamSpec spec;

    /** Where a new clip starts. The playhead's bar rather than zero: a curve
        made while listening should appear where you are. */
    std::function<int()> startBar;

    /** Called with the clip that was created, so whoever owns the tabs can show
        it. Creating something the user cannot see is worse than not offering it. */
    std::function<void (juce::ValueTree)> reveal;
};

/** The half of a Context that is the same for every control in the window.

    An editor knows which NODE a knob was built for; it does not know where the
    playhead is or which tab the arrangement is on. Rather than thread the engine
    and the tab strip into six panels, whoever owns them fills one of these in and
    hands out a pointer.

    A null host is a window with no automation menus, which is exactly what a
    test harness that builds one panel on its own should get.
*/
struct Host
{
    ProjectDocument* document = nullptr;
    std::function<int()> startBar;
    std::function<void (juce::ValueTree)> reveal;

    Context contextFor (std::function<juce::ValueTree()> owner, const ParamSpec& spec) const
    {
        return { document, std::move (owner), spec, startBar, reveal };
    }
};

/** Attaches a control's menu when there is a host, and does nothing when there
    is not - so a panel says this once rather than guarding every call site. */
template <typename Control>
void attachTo (const Host* host, Control& control, std::function<juce::ValueTree()> owner,
               const ParamSpec& spec);

/** The items, for a test to read - showMenuAsync cannot be driven headlessly, so
    every menu in dew is built by one named function and applied by another. */
juce::PopupMenu build (const juce::ValueTree& project, const juce::ValueTree& owner,
                       const ParamSpec&);

void apply (int choice, const Context&);

/** Opens the menu over a control. The one place that shows it, so the look and
    feel and the safety of the callback are stated once. */
void show (juce::Component& control, const Context&);

/** A right-click menu on a control that has no hook of its own.

    Four of dew's own controls carry onContextMenu; the mixer's faders and its M
    and S do not - they are a juce::Slider and a juce::TextButton used directly.
    A MouseListener reaches those without every one of them having to become a
    Dew control first, and JUCE delivers it to the listener directly rather than
    through a ComponentPeer, so a test can still drive it.

    The caller owns it and must outlive the control it watches.
*/
class Trigger : public juce::MouseListener
{
public:
    Trigger (juce::Component& c, Context ctx);
    ~Trigger() override;

    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Component& control;
    Context context;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigger)
};

/** Wires build+apply onto a control's own hook. ONE call site per control rather
    than eleven copies of the same lambda. */
template <typename Control>
void attach (Control& control, Context context)
{
    control.onContextMenu = [&control, context] { show (control, context); };
}

template <typename Control>
void attachTo (const Host* host, Control& control, std::function<juce::ValueTree()> owner,
               const ParamSpec& spec)
{
    if (host == nullptr || host->document == nullptr)
        return;

    attach (control, host->contextFor (std::move (owner), spec));
}

} // namespace dew::paramMenu
