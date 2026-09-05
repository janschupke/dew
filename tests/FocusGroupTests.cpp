#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/FocusGroups.h"

using namespace dew;

namespace
{

/** A component that takes the keyboard, or a container that holds some.

    Hand-built trees rather than a MainComponent, which is the whole reason the
    ring lives in dew_design: the shapes that matter here - a container that is
    empty, a container that takes focus itself, a group inside a group - are
    three lines each to build and would each need a different real panel to find.
*/
struct Node : juce::Component
{
    Node (const juce::String& id, bool takesFocus, bool container)
    {
        setComponentID (id);
        setWantsKeyboardFocus (takesFocus);

        if (container)
            setFocusContainerType (FocusContainerType::focusContainer);

        setSize (10, 10);
        setVisible (true);
    }
};

Node& add (juce::Component& parent, std::unique_ptr<Node> child, juce::OwnedArray<Node>& lifetime)
{
    auto& node = *lifetime.add (child.release());
    parent.addAndMakeVisible (node);

    return node;
}

std::unique_ptr<Node> control (const juce::String& id)
{
    return std::make_unique<Node> (id, true, false);
}

std::unique_ptr<Node> group (const juce::String& id, bool takesFocus = false)
{
    return std::make_unique<Node> (id, takesFocus, true);
}

juce::StringArray idsOf (const std::vector<juce::Component*>& components)
{
    juce::StringArray ids;

    for (auto* component : components)
        ids.add (component->getComponentID());

    return ids;
}

} // namespace

TEST_CASE ("a container with nowhere to land is not a group", "[ui][focus][a11y]")
{
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& empty = add (root, group ("empty"), lifetime);
    auto& hidden = add (empty, control ("hidden"), lifetime);
    auto& off = add (empty, control ("disabled"), lifetime);

    hidden.setVisible (false);
    off.setEnabled (false);

    CHECK_FALSE (focusGroups::isGroup (empty));
    CHECK (focusGroups::entryPointOf (empty) == nullptr);
    CHECK (focusGroups::groupsIn (root).empty());

    // Control case: the very same container is a group the moment one of its
    // controls comes back, so the emptiness test is what is being measured and
    // not some other refusal.
    hidden.setVisible (true);
    CHECK (focusGroups::isGroup (empty));
    CHECK (focusGroups::entryPointOf (empty) == &hidden);
}

TEST_CASE ("a container that takes the keyboard itself is a group", "[ui][focus][a11y]")
{
    // The effect chain's shape. It parents nothing but effect cards, each of
    // which is a group of its own, and it still calls setWantsKeyboardFocus
    // because it owns escape-cancels-a-reorder. A children-only rule drops it
    // out of the ring and leaves that handler reachable by tab alone.
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& chain = add (root, group ("chain", true), lifetime);
    auto& card = add (chain, group ("card"), lifetime);
    add (card, control ("knob"), lifetime);

    CHECK (focusGroups::isGroup (chain));
    CHECK (focusGroups::entryPointOf (chain) == &chain);

    // And it is itself the only thing in it: the card's knob belongs to the
    // card.
    CHECK (idsOf (focusGroups::targetsIn (chain)) == juce::StringArray { "chain" });
    CHECK (idsOf (focusGroups::targetsIn (card)) == juce::StringArray { "knob" });
    CHECK (idsOf (focusGroups::groupsIn (root)) == juce::StringArray { "chain", "card" });
}

TEST_CASE ("a group descends to a grandchild but stops at a nested group", "[ui][focus][a11y]")
{
    // The mixer's shape, and both halves were got wrong first time round. Its
    // add button is a GRANDchild - it lives in the strip holder, inside the
    // viewport - so a direct-children rule never finds it and the mixer stops
    // being a stop at all. But a walk that did not stop at a strip would hand
    // the mixer every fader in the window and make each strip's group pointless.
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& mixer = add (root, group ("mixer"), lifetime);
    auto& viewport = add (mixer, control ("viewport"), lifetime);
    auto& holder = add (viewport, std::make_unique<Node> ("holder", false, false), lifetime);
    add (holder, control ("addTrack"), lifetime);

    auto& strip = add (holder, group ("strip"), lifetime);
    add (strip, control ("fader"), lifetime);

    CHECK (idsOf (focusGroups::targetsIn (mixer)) == juce::StringArray { "viewport", "addTrack" });
    CHECK (idsOf (focusGroups::targetsIn (strip)) == juce::StringArray { "fader" });
    CHECK (idsOf (focusGroups::groupsIn (root)) == juce::StringArray { "mixer", "strip" });
}

TEST_CASE ("the group ring wraps in both directions", "[ui][focus][a11y]")
{
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& first = add (root, group ("first"), lifetime);
    auto& firstControl = add (first, control ("a"), lifetime);
    auto& second = add (root, group ("second"), lifetime);
    auto& secondControl = add (second, control ("b"), lifetime);
    auto& third = add (root, group ("third"), lifetime);
    auto& thirdControl = add (third, control ("c"), lifetime);

    CHECK (focusGroups::nextFocusFor (root, &firstControl, 1) == &secondControl);
    CHECK (focusGroups::nextFocusFor (root, &secondControl, 1) == &thirdControl);
    CHECK (focusGroups::nextFocusFor (root, &thirdControl, 1) == &firstControl);

    CHECK (focusGroups::nextFocusFor (root, &firstControl, -1) == &thirdControl);
    CHECK (focusGroups::nextFocusFor (root, &thirdControl, -1) == &secondControl);

    // Nothing focused enters from the end the press is heading away from.
    CHECK (focusGroups::nextFocusFor (root, nullptr, 1) == &firstControl);
    CHECK (focusGroups::nextFocusFor (root, nullptr, -1) == &thirdControl);

    // Stepping forwards and back is where you started, from every group. A ring
    // that skipped one in one direction would still pass every check above.
    for (auto* from : { &firstControl, &secondControl, &thirdControl })
        CHECK (focusGroups::nextFocusFor (root, focusGroups::nextFocusFor (root, from, 1), -1)
               == from);
}

TEST_CASE ("a ring of one lands in its only group", "[ui][focus][a11y]")
{
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& only = add (root, group ("only"), lifetime);
    auto& inside = add (only, control ("inside"), lifetime);

    CHECK (focusGroups::nextFocusFor (root, nullptr, 1) == &inside);

    // From inside it, the ring comes back to its entry point rather than
    // refusing to move - which is what a person pressing the key is asking for.
    CHECK (focusGroups::nextFocusFor (root, &inside, 1) == &inside);
}

TEST_CASE ("a dialog holding the keyboard moves nothing behind it", "[ui][focus][a11y]")
{
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& panel = add (root, group ("panel"), lifetime);
    add (panel, control ("inPanel"), lifetime);

    // A component that is not under the root at all: a dialog is a separate
    // top-level component, so the window's own ring must not answer for it.
    juce::OwnedArray<Node> elsewhere;
    Node dialog { "dialog", false, false };
    dialog.setSize (50, 50);
    auto& inDialog = add (dialog, control ("inDialog"), elsewhere);

    CHECK (focusGroups::ownerOfFocus (root, &inDialog) == nullptr);
    CHECK (focusGroups::nextFocusFor (root, &inDialog, 1) == nullptr);
    CHECK (focusGroups::nextFocusFor (root, &inDialog, -1) == nullptr);
}

TEST_CASE ("a group hidden under the keyboard falls back to an end", "[ui][focus][a11y]")
{
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& first = add (root, group ("first"), lifetime);
    auto& firstControl = add (first, control ("a"), lifetime);
    auto& second = add (root, group ("second"), lifetime);
    auto& secondControl = add (second, control ("b"), lifetime);

    REQUIRE (focusGroups::nextFocusFor (root, &secondControl, 1) == &firstControl);

    // Hide the group the keyboard is standing in. It leaves the ring, so there
    // is no index to step from - and the answer is an end, not nothing.
    second.setVisible (false);

    CHECK (focusGroups::ownerOfFocus (root, &secondControl) == nullptr);
    CHECK (focusGroups::nextFocusFor (root, &secondControl, 1) == &firstControl);
    CHECK (focusGroups::nextFocusFor (root, &secondControl, -1) == &firstControl);
}

TEST_CASE ("a component outside every group belongs to no group", "[ui][focus][a11y]")
{
    // This is the shape the window-wide gate refuses: a control parented
    // straight onto the root, which is deliberately NOT a focus container so
    // that the gate has something it can fail on.
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& orphan = add (root, control ("orphan"), lifetime);
    auto& panel = add (root, group ("panel"), lifetime);
    auto& adopted = add (panel, control ("adopted"), lifetime);

    CHECK (focusGroups::ownerOfFocus (root, &orphan) == nullptr);
    CHECK (focusGroups::ownerOfFocus (root, &adopted) == &panel);

    // JUCE's own findFocusContainer would hand back the top-level component
    // whether or not it is a container, which is why the ring cannot be built
    // on it and why MainComponent must never become one.
    CHECK (orphan.findFocusContainer() == &root);
    CHECK_FALSE (root.isFocusContainer());
}

TEST_CASE ("a container JUCE declared for itself is not a group", "[ui][focus][a11y]")
{
    // juce::Label::setEditable makes a label a keyboardFocusContainer
    // (juce_Label.cpp:120), and keyboardFocusContainer implies focusContainer
    // (juce_Component.cpp:2881) - so reading isFocusContainer() alone put every
    // editable NAME in the window into the ring as a group of its own: seven of
    // them, each a ctrl-tab stop on a text label sitting inside the group it
    // belongs to. ScrollBar, TabbedButtonBar and PropertyPanel's viewport are
    // the same shape.
    //
    // dew declares the plain focusContainer and never the keyboard one, which is
    // what tells the two apart.
    juce::OwnedArray<Node> lifetime;
    Node root { "root", false, false };
    root.setSize (100, 100);

    auto& header = add (root, group ("header"), lifetime);
    auto& name = add (header, control ("name"), lifetime);

    juce::Label editable;
    editable.setEditable (false, true, false);
    header.addAndMakeVisible (editable);

    REQUIRE (editable.isFocusContainer());
    REQUIRE (editable.isKeyboardFocusContainer());

    CHECK_FALSE (focusGroups::isGroup (editable));
    CHECK (idsOf (focusGroups::groupsIn (root)) == juce::StringArray { "header" });

    // And it is not skipped either: the label belongs to the header's group, so
    // the keyboard still reaches it with plain tab once ctrl-tab has arrived.
    const auto targets = focusGroups::targetsIn (header);
    CHECK (targets.size() == 2);
    CHECK (targets.front() == &name);
    CHECK (targets.back() == &editable);
}
