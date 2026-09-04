// The panel's slot selector.
//
// Split out of OscillatorTests.cpp along the tags it already carried. The
// shared helpers are OscillatorHarness.h; the engine and UI groups each kept
// the fixture that sat directly above them.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/SynthChannel.h"
#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "ui/EditorState.h"
#include "ui/OscillatorSection.h"

#include "FixtureProject.h"
#include "OscillatorHarness.h"

using namespace dew;
using namespace dew::testing;
using Catch::Approx;

namespace
{

struct OscHarness
{
    OscHarness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        section.setSize (300, OscillatorSection::heightFor (false));
        section.setVisible (true);
        section.setOwner (channel().getChildWithName (ids::INSTRUMENT));
    }

    juce::ValueTree channel()
    {
        return firstChannel (document.getState());
    }
    juce::ValueTree slot (int i)
    {
        return ProjectEdits::oscillatorAt (channel(), i);
    }

    ProjectDocument document;
    EditorState editorState;
    OscillatorSection section { document, editorState };
};

} // namespace

TEST_CASE ("the panel shows every oscillator slot, with only the first switched on",
           "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    REQUIRE (h.section.getNumSlots() == kMaxOscillators);
    REQUIRE (h.section.getSelectedSlot() == 0);
    REQUIRE (h.section.isSlotEnabled (0));
    REQUIRE (! h.section.isSlotEnabled (1));
    REQUIRE (! h.section.isSlotEnabled (2));
}

TEST_CASE ("selecting a slot changes which oscillator the controls edit", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    const auto originalFirstWave = h.slot (0)[ids::wave].toString();

    h.section.selectSlot (1);
    REQUIRE (h.section.getSelectedSlot() == 1);

    // Invoked directly rather than through the widget: no message loop runs in
    // a console test, so nothing would deliver the change.
    h.section.getWaveBox().setSelectedId (3, juce::dontSendNotification);
    h.section.getWaveBox().onChange();

    REQUIRE (h.slot (1)[ids::wave].toString() == "square");
    REQUIRE (h.slot (0)[ids::wave].toString() == originalFirstWave);
}

TEST_CASE ("selecting a slot is not an edit", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (2);

    // Which slot is showing is view state. It must not dirty the project, and
    // it must not land on the undo stack in front of the user's real edits.
    REQUIRE (! h.document.hasChangedSinceSaved());
    REQUIRE (! h.document.getUndoManager().canUndo());
}

TEST_CASE ("switching an oscillator on is one undo step", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (1);
    h.section.getEnableButton().onClick();

    REQUIRE (h.section.isSlotEnabled (1));
    REQUIRE ((bool) h.slot (1)[ids::enabled] == true);
    REQUIRE (h.document.getUndoManager().canUndo());

    h.document.getUndoManager().undo();

    REQUIRE (! h.section.isSlotEnabled (1));
}

TEST_CASE ("switching an oscillator on leaves its settings alone", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (1);
    h.section.getWaveBox().setSelectedId (1, juce::dontSendNotification);
    h.section.getWaveBox().onChange();

    h.section.setSlotEnabled (1, true);
    h.section.setSlotEnabled (1, false);
    h.section.setSlotEnabled (1, true);

    // A slot you switch off keeps the sound you gave it, so switching it back
    // on returns what you had rather than a fresh default.
    REQUIRE (h.slot (1)[ids::wave].toString() == "sine");
}

TEST_CASE ("the header follows every slot, the controls follow one", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    const auto firstWaveId = h.section.getWaveBox().getSelectedId();

    // An edit to a slot that is not showing, made from outside the component.
    h.slot (2).setProperty (ids::enabled, true, nullptr);

    REQUIRE (h.section.isSlotEnabled (2));
    REQUIRE (h.section.getWaveBox().getSelectedId() == firstWaveId);
}

TEST_CASE ("another channel's oscillators are not this section's business", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    const auto firstWaveId = h.section.getWaveBox().getSelectedId();

    auto other = ProjectEdits::findChannel (h.document.getState(), 2);
    REQUIRE (other.isValid());

    // The filter this replaced matched by node TYPE, so this edit refreshed a
    // panel showing an entirely different channel.
    auto otherOsc = ProjectEdits::oscillatorAt (other, 0);
    otherOsc.setProperty (ids::wave, otherOsc[ids::wave].toString() == "sine" ? "saw" : "sine",
                          nullptr);

    REQUIRE (h.section.getWaveBox().getSelectedId() == firstWaveId);
}

TEST_CASE ("the selected slot never leaves the slots that exist", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.selectSlot (99);
    REQUIRE (h.section.getSelectedSlot() == kMaxOscillators - 1);

    h.section.selectSlot (-5);
    REQUIRE (h.section.getSelectedSlot() == 0);
}

TEST_CASE ("the section survives having no channel", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.setOwner ({});
    h.section.resized();

    REQUIRE (h.section.getNumSlots() == 0);
    REQUIRE (! h.section.isSlotEnabled (0));

    // Still paints, rather than asserting its way out of an empty panel.
    REQUIRE (renderToImage (h.section).isValid());
}

TEST_CASE ("the section paints its slots and its controls", "[ui][oscillator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    OscHarness h;

    h.section.resized();

    REQUIRE (fractionOfNonBackgroundPixels (renderToImage (h.section)) > 0.05f);
}
