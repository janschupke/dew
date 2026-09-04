#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/MainComponent.h"
#include "ui/TransportBar.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

/** Counts callbacks from a tree, standing in for any UI component that listens
    to the document and repaints.

    It listens through a REFERENCE, the way the UI components do
    (`document.getState().addListener (this)`), because a juce::ValueTree's
    listener list belongs to that particular ValueTree instance rather than to
    the shared underlying object. Registering on a temporary copy silently
    unregisters when the copy dies.
*/
struct CountingListener : private juce::ValueTree::Listener
{
    void listenTo (juce::ValueTree& tree)
    {
        tree.addListener (this);
    }
    void stopListening (juce::ValueTree& tree)
    {
        tree.removeListener (this);
    }

    int propertyChanges = 0;
    int childrenAdded = 0;
    int redirections = 0;

private:
    void valueTreeRedirected (juce::ValueTree&) override
    {
        ++redirections;
    }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override
    {
        ++propertyChanges;
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override
    {
        ++childrenAdded;
    }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}
};

/** Component::findChildWithID only looks at direct children. */
juce::Component* findDescendantWithID (juce::Component& parent, const juce::String& id)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findDescendantWithID (*child, id))
            return found;
    }

    return nullptr;
}

} // namespace

namespace
{

/** The pattern selector, found by its component ID.

    By type would be ambiguous - the transport bar has carried more than one
    ComboBox since it gained a time signature - and findChildWithID is not
    recursive, so this walks.
*/
juce::ComboBox* findPatternSelector (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == "patternSelector")
            if (auto* asBox = dynamic_cast<juce::ComboBox*> (child))
                return asBox;

        if (auto* found = findPatternSelector (*child))
            return found;
    }

    return nullptr;
}

} // namespace

TEST_CASE ("a listener registered once keeps working after the document is replaced",
           "[document][identity]")
{
    // juce::ValueTree::operator= migrates listeners to the new object and fires
    // valueTreeRedirected, so a component that registered on document.getState()
    // keeps working across File > New and Open without re-registering. This
    // pins that, because the UI depends on it and it is not obvious.
    ProjectDocument document;

    CountingListener listener;
    listener.listenTo (document.getState());

    document.getState().setProperty (ids::name, "before", nullptr);
    REQUIRE (listener.propertyChanges == 1);

    // What File > Open does.
    document.setState (dew::testing::fixtureProject(), true);

    document.getState().setProperty (ids::name, "after", nullptr);
    REQUIRE (listener.propertyChanges > 1);

    auto pattern = ProjectEdits::addPattern (document.getState(), nullptr);
    REQUIRE (pattern.isValid());
    REQUIRE (listener.childrenAdded > 0);

    listener.stopListening (document.getState());
}

TEST_CASE ("the pattern dropdown offers a way to make one", "[ui][transport]")
{
    // The + beside the box is easy to miss when the box is what you were
    // already looking at, so the list someone opens looking for their patterns
    // is also where a new one comes from.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1280, 800);

    REQUIRE_FALSE (component.getChildren().isEmpty());
    auto* box = findPatternSelector (*component.getChildren().getFirst());
    REQUIRE (box != nullptr);

    REQUIRE (box->indexOfItemId (TransportBar::newPatternItemId) >= 0);
    CHECK (box->getItemText (box->indexOfItemId (TransportBar::newPatternItemId)) == "New pattern");

    const auto countPatterns = [&component]
    {
        int n = 0;

        for (const auto& child : component.getDocument().getState())
            if (child.hasType (ids::PATTERN))
                ++n;

        return n;
    };

    const auto before = countPatterns();
    const auto wasCurrent = component.getDocument().getState().isValid();
    REQUIRE (wasCurrent);

    // Choosing it makes a pattern rather than selecting a row called one.
    box->setSelectedId (TransportBar::newPatternItemId, juce::sendNotificationSync);

    CHECK (countPatterns() == before + 1);

    // And leaves the box showing the new pattern, not the sentinel.
    CHECK (box->getSelectedId() != TransportBar::newPatternItemId);
    CHECK (box->getSelectedId() > 0);
}

TEST_CASE ("the transport bar still tracks the project after New and Open",
           "[document][identity][ui]")
{
    // Guards the path behind "patterns - can't add more": open a file, add a
    // pattern, and the selector must show it. It does, which is how we know
    // that report is about a missing button rather than a broken document.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.setSize (1280, 800);

    auto& document = component.getDocument();

    const auto patternCountInBox = [&component]
    {
        // Only search the transport bar, which is the first child.
        if (component.getChildren().isEmpty())
            return -1;

        auto* box = findPatternSelector (*component.getChildren().getFirst());

        if (box == nullptr)
            return -1;

        // The list ends in "New pattern", which is a way to make one rather
        // than one of them.
        int patterns = 0;

        for (int i = 0; i < box->getNumItems(); ++i)
            if (box->getItemId (i) != TransportBar::newPatternItemId)
                ++patterns;

        return patterns;
    };

    REQUIRE (patternCountInBox() == 1);

    // Replace the document, as File > Open does, then add a pattern.
    document.setState (dew::testing::fixtureProject(), true);
    component.documentWasReplaced();

    REQUIRE (patternCountInBox() == 1);

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add pattern");
    ProjectEdits::addPattern (document.getState(), &undo);

    REQUIRE (patternCountInBox() == 2);
}

namespace
{

/** Builds a MouseEvent the way the framework would, so a component's handler
    can be driven directly in a headless test.
*/
juce::MouseEvent clickAt (juce::Component& target, juce::Point<int> local, int clickCount = 1,
                          juce::ModifierKeys mods = juce::ModifierKeys())
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position,
             mods,
             1.0f,
             0.0f,
             0.0f,
             0.0f,
             0.0f,
             &target,
             &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             clickCount,
             false };
}

int countNotes (const juce::ValueTree& pattern)
{
    int n = 0;

    for (const auto& child : pattern)
        if (child.hasType (ids::NOTE))
            ++n;

    return n;
}

} // namespace

TEST_CASE ("clicks land on the step grid across window sizes", "[ui][hittest]")
{
    // "Channel rack clicks don't go through." Two separate things have to hold:
    // the grid must be the component under the cursor, and clicking it must
    // actually write a note.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    component.getDocument().setState (ProjectFactory::createDefault(), true);
    component.documentWasReplaced();

    // getComponentAt walks only visible components; without this the whole tree
    // reports nothing under the cursor and the test proves nothing.
    component.setVisible (true);

    for (auto size : { juce::Point<int> { 1000, 640 }, juce::Point<int> { 1280, 800 },
                       juce::Point<int> { 1920, 1200 } })
    {
        component.setSize (size.x, size.y);

        auto* grid = findDescendantWithID (component, "stepGrid");
        INFO ("size " << size.x << "x" << size.y);
        REQUIRE (grid != nullptr);
        REQUIRE (grid->getWidth() > 100);
        REQUIRE (grid->getHeight() > 0);

        for (int row = 0; row < 4; ++row)
        {
            for (int step = 0; step < 16; ++step)
            {
                const auto local = juce::Point<int> (
                    (int) ((step + 0.5f) * (float) grid->getWidth() / 16.0f),
                    (int) ((row + 0.5f) * 26.0f));

                auto* hit = component.getComponentAt (component.getLocalPoint (grid, local));

                INFO (
                    "row " << row << " step " << step << " -> "
                           << (hit != nullptr ? hit->getComponentID() : juce::String ("nullptr")));
                REQUIRE (hit == grid);
            }
        }
    }
}

TEST_CASE ("clicking a step writes a note, and right-clicking clears it", "[ui][hittest]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component (false);
    auto& document = component.getDocument();
    document.setState (ProjectFactory::createDefault(), true);
    component.documentWasReplaced();
    component.setVisible (true);
    component.setSize (1280, 800);

    auto* grid = findDescendantWithID (component, "stepGrid");
    REQUIRE (grid != nullptr);

    auto pattern = ProjectEdits::findPattern (document.getState(), 1);
    REQUIRE (pattern.isValid());
    REQUIRE (countNotes (pattern) == 0);

    // Row 1 (the second channel), step 5.
    const auto local = juce::Point<int> ((int) ((5 + 0.5f) * (float) grid->getWidth() / 16.0f), 39);

    grid->mouseDown (clickAt (*grid, local));
    REQUIRE (countNotes (pattern) == 1);

    const auto note = ProjectEdits::findNoteAtStep (pattern, 2, 5);
    REQUIRE (note.isValid());

    // Clicking it again leaves it alone. It used to toggle, which made the
    // ordinary way of looking at a pattern - clicking around it - delete the
    // thing that was clicked.
    grid->mouseDown (clickAt (*grid, local));
    REQUIRE (countNotes (pattern) == 1);

    // Taking a step back is a right-click, the way it is over the piano roll.
    grid->mouseDown (
        clickAt (*grid, local, 1, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
    REQUIRE (countNotes (pattern) == 0);
}
