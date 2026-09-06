#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ControlWalkHarness.h"
#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/ConfirmPanel.h"
#include "ui/EditorState.h"
#include "ui/TransportBar.h"

#include "ConfirmSupport.h"
#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

namespace
{

struct TransportHarness
{
    TransportHarness()
    {
        document.setState (ProjectFactory::createDefault(), true);
        bar.confirmDestructive = recorder.hook();
        bar.setSize (1200, tokens::size::stripTransport);
        bar.setVisible (true);
        bar.refresh();
        bar.resized();
    }

    int countPatterns() const
    {
        int n = 0;

        for (const auto& child : document.getState())
            if (child.hasType (ids::PATTERN))
                ++n;

        return n;
    }

    juce::Button& deleteButton()
    {
        auto* found = findDescendantWithID (bar, "deletePattern");
        REQUIRE (found != nullptr);
        return *dynamic_cast<juce::Button*> (found);
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    ConfirmRecorder recorder;
    TransportBar bar { document, engine, editorState };
};

} // namespace

TEST_CASE ("the confirmation runs its action only when confirmed", "[ui][confirm]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Built bare, with no DialogWindow above it: closeDialog finds no parent and
    // does nothing, which is exactly what makes the buttons drivable here.
    const ConfirmPanel::Request request { "Delete pattern", "Delete \"Groove\"?", "Delete" };

    SECTION ("cancel runs nothing")
    {
        ConfirmPanel panel (request);
        auto ran = 0;
        panel.onConfirm = [&ran] { ++ran; };

        panel.getCancelButton().onClick();
        CHECK (ran == 0);
    }

    SECTION ("confirm runs it once")
    {
        ConfirmPanel panel (request);
        auto ran = 0;
        panel.onConfirm = [&ran] { ++ran; };

        panel.getConfirmButton().onClick();
        CHECK (ran == 1);
    }
}

TEST_CASE ("the confirmation says what it was asked to say", "[ui][confirm]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ConfirmPanel panel ({ "Remove channel", "Remove \"Kick\"? Its notes go too.", "Remove" });
    panel.setSize (ConfirmPanel::preferredWidth, ConfirmPanel::preferredHeight);

    CHECK (panel.getConfirmButton().getButtonText() == "Remove");
    CHECK (panel.getCancelButton().getButtonText() == "Cancel");

    // And the sentence is actually painted, not merely stored.
    CHECK (inkCoverage (render (panel)) > 0.0f);
}

TEST_CASE ("deleting a pattern asks first, and changes nothing until answered", "[ui][confirm]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TransportHarness h;

    juce::UndoManager scratch;
    ProjectEdits::addPattern (h.document.getState(), &scratch);
    h.bar.refresh();
    h.bar.resized();

    const auto before = h.countPatterns();
    REQUIRE (before == 2);

    h.deleteButton().onClick();

    REQUIRE (h.recorder.timesAsked == 1);
    CHECK (h.recorder.lastRequest.title == "Delete pattern");

    // The question names the pattern, so a person can tell which one they are
    // about to lose - and says what goes with it, because removePattern deletes
    // every clip that played it.
    const auto current = ProjectEdits::findPattern (h.document.getState(),
                                                    h.editorState.getCurrentPatternId());
    REQUIRE (current.isValid());
    CHECK (h.recorder.lastRequest.message.contains (current[ids::name].toString()));
    CHECK (h.recorder.lastRequest.message.contains ("clip"));

    // Nothing has happened yet. This is the assertion the whole hook exists for.
    CHECK (h.countPatterns() == before);
    CHECK_FALSE (h.document.getUndoManager().canUndo());

    h.recorder.confirm();

    CHECK (h.countPatterns() == before - 1);
    REQUIRE (h.document.getUndoManager().undo());
    CHECK (h.countPatterns() == before);
}

TEST_CASE ("cancelling a pattern deletion leaves the document alone", "[ui][confirm]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TransportHarness h;

    juce::UndoManager scratch;
    ProjectEdits::addPattern (h.document.getState(), &scratch);
    h.bar.refresh();
    h.bar.resized();

    const auto before = h.countPatterns();
    h.deleteButton().onClick();
    REQUIRE (h.recorder.timesAsked == 1);

    // The answer is dropped on the floor, which is what cancel does.
    CHECK (h.countPatterns() == before);
    CHECK_FALSE (h.document.getUndoManager().canUndo());
}

TEST_CASE ("the delete button is off when there is only one pattern", "[ui][confirm]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    TransportHarness h;

    // The defect: the button read its enabled state off the pattern BOX, which
    // carries a "New pattern" row of its own below a separator. With one
    // pattern getNumItems() came back 2, so the button was enabled, the click
    // fired, and removePattern refused - a button that silently did nothing.
    REQUIRE (h.countPatterns() == 1);
    CHECK_FALSE (h.deleteButton().isEnabled());

    // Asking is not enough: a disabled button would still have been asked if
    // the guard lived only in the paint.
    h.deleteButton().onClick();
    CHECK (h.recorder.timesAsked == 0);
    CHECK (h.countPatterns() == 1);

    juce::UndoManager scratch;
    ProjectEdits::addPattern (h.document.getState(), &scratch);
    h.bar.refresh();
    h.bar.resized();

    REQUIRE (h.countPatterns() == 2);
    CHECK (h.deleteButton().isEnabled());
}
