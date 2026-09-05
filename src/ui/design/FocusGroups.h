#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

/** Moving the keyboard a whole COMPONENT at a time.

    Tab reaches every control in dew and has since the accessibility work, which
    is the property a gate holds. What it is not is usable: there are 264
    controls across the five tabs, so the third effect card's cutoff is dozens of
    presses from the transport bar, and a screen reader met the mixer as one flat
    run of faders. Twelve panels already declared themselves focus containers and
    nothing was bound to it, so the flag shaped the accessibility tree and bought
    the keyboard nothing at all.

    Ctrl-tab and ctrl-shift-tab now step over a group. That key was the only
    mod-tab free on all three platforms dew ships: command-tab is the macOS
    application switcher, and alt-tab is the window switcher on Windows and on
    every mainstream Linux desktop. It cost the editor-tab cycling that used to
    hold it, which is no loss - command-1 to command-5 and the View menu both
    still switch tabs, and those two rows are keyless now the way the scales and
    the themes are.

    A group is what a screen reader already calls a group. That is not a pun:
    setFocusContainerType (focusContainer) sets isFocusContainerFlag and leaves
    isKeyboardFocusContainerFlag alone (juce_Component.cpp:2881), so the flag was
    only ever an accessibility fact, and reading it here is what makes the two
    agree instead of dew keeping a second list.
*/
namespace dew::focusGroups
{

/** Everything the keyboard can land on inside `container` without leaving it.

    Two rules, and each one was got wrong on the first attempt:

      - **The container itself counts** when it takes focus. The effect chain
        parents nothing but cards and still calls setWantsKeyboardFocus, because
        it owns escape-cancels-a-reorder. A children-only rule drops it out of
        the ring entirely and leaves that handler reachable by tab alone.

      - **The walk descends, and stops at a nested container.** The mixer's add
        button is a GRANDchild - it lives in the strip holder, inside the
        viewport - so a direct-children rule would not find it. But a walk that
        did not stop would hand the mixer eleven strips' worth of faders and
        make each strip's own group pointless.

    In child order, not screen position: resized() may not have run when this is
    asked, and add order already is reading order everywhere it matters. JUCE's
    own tab traversal WITHIN a group is position-sorted, so the two can disagree
    in a layout that fights its own add order.
*/
std::vector<juce::Component*> targetsIn (juce::Component& container);

/** Whether this component is a stop: a container DEW declared, with somewhere
    to land.

    Two conditions, and the first is not the obvious one. isFocusContainer() by
    itself is wrong, because JUCE sets that flag for its own reasons:
    juce::Label::setEditable makes an editable label a keyboardFocusContainer,
    and ScrollBar, TabbedButtonBar and PropertyPanel's viewport do the same -
    and keyboardFocusContainer implies focusContainer. Believing the one flag
    put every channel, track and strip NAME in the ring as a group of its own.

    dew always declares the plain focusContainer and never the keyboard one,
    deliberately and for an unrelated reason - a keyboard container confines tab
    with no key to leave it, which is a trap - so that choice is also what tells
    dew's groups from JUCE's.

    The emptiness test is the second: it keeps a container that has been emptied
    - every child hidden, every control disabled - from being a stop that does
    nothing when you land on it.
*/
bool isGroup (juce::Component& component);

/** Where the keyboard lands when it arrives, or null for an empty group. */
juce::Component* entryPointOf (juce::Component& group);

/** Every group under `root`, in child order, outermost first.

    A group that contains groups is still one - the mixer keeps its add button
    and its viewport after each strip becomes a stop of its own.

    Visibility is isVisible(), tested on each DESCENDANT as the walk goes down
    and never on `root` itself. Two traps, one either side:

      - isShowing() consults the ComponentPeer, and a headless harness has none,
        so a ring built on it is empty and every test over it passes for the
        wrong reason.
      - a juce::Component is constructed INVISIBLE, and it is addAndMakeVisible
        that turns a child on - so a window built in a harness and never
        parented is invisible while everything inside it is fine. Testing the
        root the way a child is tested returns nothing at all, which is how this
        was found: eight synthetic-tree tests passed and the real window had no
        groups in it whatsoever.
*/
std::vector<juce::Component*> groupsIn (juce::Component& root);

/** The group `focused` sits in, or null when it sits in none - which is both
    "nothing is focused" and "the focused thing is not under this root at all",
    the second being a modal dialog holding the keyboard. */
juce::Component* ownerOfFocus (juce::Component& root, juce::Component* focused);

/** Where the keyboard should go next, or null for nowhere.

    `focused` is a PARAMETER rather than read from getCurrentlyFocusedComponent
    inside, and that is the whole testing seam: grabKeyboardFocus is inert
    without a peer and getCurrentlyFocusedComponent stays null for ever in a
    headless harness, so a function that asked for itself could never be driven.

    The ring wraps. With nothing focused it returns the first group going
    forwards and the last going back; with the focused component under a group
    that has since been hidden it does the same, rather than refusing to move.
*/
juce::Component* nextFocusFor (juce::Component& root, juce::Component* focused, int delta);

/** nextFocusFor, then the two calls that need a peer to mean anything. Not
    reachable from a test; keep everything that is testable above it. */
void moveFocus (juce::Component& root, int delta);

} // namespace dew::focusGroups
