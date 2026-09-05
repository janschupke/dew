#include "ui/design/FocusGroups.h"

#include "ui/design/Focus.h"

#include <algorithm>

namespace dew::focusGroups
{

namespace
{

/** A DESCENDANT the walk is allowed to look at or descend into.

    Never asked of the node a walk was started from, and that is not a shortcut.
    A juce::Component is constructed INVISIBLE - addAndMakeVisible is what turns
    a child on - so a MainComponent built in a harness and never parented is
    invisible itself while every one of its children is fine. Testing the root
    the way a child is tested returns an empty ring for the whole window, which
    is the same shape of mistake as reaching for isShowing().

    Whether the thing you asked about is on screen is the caller's business.
    Whether the things inside it are is this walk's.
*/
bool isLiveChild (const juce::Component& component)
{
    return component.isVisible() && component.isEnabled();
}

/** A container dew declared, as opposed to one JUCE set for its own reasons.

    isFocusContainer() alone is NOT the question, and believing it was put seven
    phantom groups in the ring: juce::Label::setEditable makes an editable label a
    keyboardFocusContainer (juce_Label.cpp:120), and so are ScrollBar,
    TabbedButtonBar and PropertyPanel's viewport. Every flag that sets
    isKeyboardFocusContainerFlag sets isFocusContainerFlag too
    (juce_Component.cpp:2881), so every channel, track and strip NAME was
    arriving as a group of its own - a ctrl-tab stop on a text label, sitting
    inside the group it belongs to.

    dew's own convention is the other value, and it is written down where it is
    used: "focusContainer, not keyboardFocusContainer - the second would confine
    the tab key to this panel with no key to leave it, a keyboard trap". So the
    distinction dew was already making for a different reason is exactly the one
    that tells its groups from JUCE's, and this is where it earns its keep.

    The structural half only - never isGroup, which asks this in turn.
*/
bool isDeclaredContainer (const juce::Component& component)
{
    return component.isFocusContainer() && ! component.isKeyboardFocusContainer();
}

void collectTargets (juce::Component& node, bool isTheContainer, std::vector<juce::Component*>& out)
{
    // A nested container owns its own contents. The container this walk STARTED
    // from is exempt from both tests: from this one because it is the thing
    // being asked about, and from the visibility test above it for the reason
    // isLiveChild gives.
    if (! isTheContainer && (isDeclaredContainer (node) || ! isLiveChild (node)))
        return;

    if (node.getWantsKeyboardFocus() && node.isEnabled())
        out.push_back (&node);

    for (auto* child : node.getChildren())
        collectTargets (*child, false, out);
}

void collectGroups (juce::Component& node, bool isTheRoot, std::vector<juce::Component*>& out)
{
    if (! isTheRoot && ! isLiveChild (node))
        return;

    if (isGroup (node))
        out.push_back (&node);

    // Descended into either way: groups nest, and the mixer is still a stop
    // once each of its strips is one.
    for (auto* child : node.getChildren())
        collectGroups (*child, false, out);
}

bool isUnder (const juce::Component& root, const juce::Component* component)
{
    for (const auto* node = component; node != nullptr; node = node->getParentComponent())
        if (node == &root)
            return true;

    return false;
}

int indexOf (const std::vector<juce::Component*>& groups, const juce::Component* group)
{
    const auto found = std::find (groups.begin(), groups.end(), group);

    return found == groups.end() ? -1 : (int) std::distance (groups.begin(), found);
}

} // namespace

std::vector<juce::Component*> targetsIn (juce::Component& container)
{
    std::vector<juce::Component*> targets;
    collectTargets (container, true, targets);

    return targets;
}

bool isGroup (juce::Component& component)
{
    return isDeclaredContainer (component) && ! targetsIn (component).empty();
}

juce::Component* entryPointOf (juce::Component& group)
{
    const auto targets = targetsIn (group);

    return targets.empty() ? nullptr : targets.front();
}

std::vector<juce::Component*> groupsIn (juce::Component& root)
{
    std::vector<juce::Component*> groups;
    collectGroups (root, true, groups);

    return groups;
}

juce::Component* ownerOfFocus (juce::Component& root, juce::Component* focused)
{
    if (focused == nullptr || ! isUnder (root, focused))
        return nullptr;

    const auto groups = groupsIn (root);

    for (auto* node = focused; node != nullptr; node = node->getParentComponent())
    {
        if (indexOf (groups, node) >= 0)
            return node;

        // The root is where the search ends. Component::findFocusContainer
        // would keep going and hand back the top-level component whether or not
        // it is a container, which is also why MainComponent must not become
        // one - the gate that says every control belongs to a group could then
        // never fail.
        if (node == &root)
            break;
    }

    return nullptr;
}

juce::Component* nextFocusFor (juce::Component& root, juce::Component* focused, int delta)
{
    if (delta == 0)
        return nullptr;

    // A modal dialog holds the keyboard, and the window behind it must not move
    // underneath it.
    if (focused != nullptr && ! isUnder (root, focused))
        return nullptr;

    const auto groups = groupsIn (root);

    if (groups.empty())
        return nullptr;

    const auto at = indexOf (groups, ownerOfFocus (root, focused));

    // Nothing focused, or focused inside no group at all - which also covers a
    // group hidden since the keyboard arrived in it. Enter from the end the
    // press was heading away from.
    if (at < 0)
        return entryPointOf (*(delta > 0 ? groups.front() : groups.back()));

    const auto count = (int) groups.size();
    const auto next = ((at + delta) % count + count) % count;

    // A ring of one lands IN its only group rather than doing nothing, which is
    // what a person pressing the key is asking for.
    return entryPointOf (*groups[(size_t) next]);
}

void moveFocus (juce::Component& root, int delta)
{
    auto* target = nextFocusFor (root, juce::Component::getCurrentlyFocusedComponent(), delta);

    if (target == nullptr)
        return;

    // Before the grab, not after. A component with no focusGained override never
    // reports what moved the keyboard onto it, so the ring would keep whatever
    // the last focus change happened to set - a mouse click, most likely, which
    // draws no ring at all.
    focus::noteFocusChange (juce::Component::focusChangedByTabKey);
    target->grabKeyboardFocus();

    // Deliberately no postAnnouncement. The three canvases announce a cursor
    // move because a cursor move is not a focus change and nothing else in the
    // system would say it. This IS a focus change, into a real accessibility
    // group, and both VoiceOver and NVDA name the group they have entered - so
    // announcing here makes the platform say it twice.
}

} // namespace dew::focusGroups
