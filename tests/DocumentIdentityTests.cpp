#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/MainComponent.h"

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
    void listenTo (juce::ValueTree& tree) { tree.addListener (this); }
    void stopListening (juce::ValueTree& tree) { tree.removeListener (this); }

    int propertyChanges = 0;
    int childrenAdded = 0;
    int redirections = 0;

private:
    void valueTreeRedirected (juce::ValueTree&) override { ++redirections; }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { ++propertyChanges; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { ++childrenAdded; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              {}
    void valueTreeParentChanged (juce::ValueTree&) override                            {}
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

TEST_CASE ("a listener registered once keeps working after the document is replaced", "[document][identity]")
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
    document.setState (ProjectFactory::createDemo(), true);

    document.getState().setProperty (ids::name, "after", nullptr);
    REQUIRE (listener.propertyChanges > 1);

    auto pattern = ProjectEdits::addPattern (document.getState(), nullptr);
    REQUIRE (pattern.isValid());
    REQUIRE (listener.childrenAdded > 0);

    listener.stopListening (document.getState());
}

TEST_CASE ("the transport bar still tracks the project after New and Open", "[document][identity][ui]")
{
    // Guards the path behind "patterns - can't add more": open a file, add a
    // pattern, and the selector must show it. It does, which is how we know
    // that report is about a missing button rather than a broken document.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component;
    component.setSize (1280, 800);

    auto& document = component.getDocument();

    const auto patternCountInBox = [&component]
    {
        juce::ComboBox* box = nullptr;

        // The transport bar's only ComboBox is the pattern selector.
        std::function<void (juce::Component&)> findBox = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* asBox = dynamic_cast<juce::ComboBox*> (child); asBox != nullptr && box == nullptr)
                    box = asBox;
                else
                    findBox (*child);
            }
        };

        // Only search the transport bar, which is the first child.
        if (! component.getChildren().isEmpty())
            findBox (*component.getChildren().getFirst());

        return box != nullptr ? box->getNumItems() : -1;
    };

    REQUIRE (patternCountInBox() == 1);

    // Replace the document, as File > Open does, then add a pattern.
    document.setState (ProjectFactory::createDemo(), true);
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
juce::MouseEvent clickAt (juce::Component& target, juce::Point<int> local, int clickCount = 1)
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position,
             juce::ModifierKeys(),
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &target, &target,
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

    MainComponent component;
    component.getDocument().setState (ProjectFactory::createDefault(), true);
    component.documentWasReplaced();

    // getComponentAt walks only visible components; without this the whole tree
    // reports nothing under the cursor and the test proves nothing.
    component.setVisible (true);

    for (auto size : { juce::Point<int> { 1000, 640 },
                       juce::Point<int> { 1280, 800 },
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

                INFO ("row " << row << " step " << step << " -> "
                      << (hit != nullptr ? hit->getComponentID() : juce::String ("nullptr")));
                REQUIRE (hit == grid);
            }
        }
    }
}

TEST_CASE ("clicking a step writes a note, and clicking it again clears it", "[ui][hittest]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component;
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

    grid->mouseDown (clickAt (*grid, local));
    REQUIRE (countNotes (pattern) == 0);
}
