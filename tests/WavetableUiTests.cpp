// The oscillator panel's second face.
//
// Split out of a WavetableTests.cpp that was 1,046 lines. It already carried
// three fixture blocks, one immediately before each group of tags, so each
// file takes its own with it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/SynthChannel.h"
#include "engine/Wavetable.h"
#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSerializer.h"
#include "ui/OscillatorSection.h"

#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

juce::Image renderToImage (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                       juce::jmax (1, c.getHeight()), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

float fractionOfNonBackgroundPixels (const juce::Image& image)
{
    const auto background = image.getPixelAt (0, 0);
    int differing = 0;

    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            if (image.getPixelAt (x, y) != background)
                ++differing;

    const auto sampled = (image.getWidth() / 2) * (image.getHeight() / 2);
    return sampled > 0 ? (float) differing / (float) sampled : 0.0f;
}

struct PanelHarness
{
    PanelHarness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        section.onHeightChanged = [this]
        {
            ++heightChanges;
            layOut();
        };
        section.setVisible (true);
        section.setOwner (channel().getChildWithName (ids::INSTRUMENT));
        layOut();
    }

    void layOut()
    {
        section.setSize (300, section.getRequiredHeight());
        section.resized();
    }

    /** Drives the box the way a user would, minus the message loop a console
        test does not have to deliver the change.
    */
    void choose (juce::ComboBox& box, const juce::String& itemText)
    {
        for (int i = 0; i < box.getNumItems(); ++i)
        {
            if (box.getItemText (i) != itemText)
                continue;

            box.setSelectedId (box.getItemId (i), juce::dontSendNotification);
            box.onChange();
            return;
        }

        FAIL ("no item called " << itemText);
    }

    /** Selects a slot and lets the change actually arrive.

        EditorState is a ChangeBroadcaster, so selectSlot() only ARMS the
        refresh - with no message loop in a console test nothing would ever run
        it, and every assertion after this would hold for the wrong reason.
    */
    void select (int index)
    {
        section.selectSlot (index);
        editorState.dispatchPendingMessages();
    }

    juce::ValueTree channel()
    {
        return document.getState().getChildWithName (ids::CHANNEL);
    }
    juce::ValueTree slot (int i)
    {
        return ProjectEdits::oscillatorAt (channel(), i);
    }

    int heightChanges = 0;
    ProjectDocument document;
    EditorState editorState;
    OscillatorSection section { document, editorState };
};

} // namespace

TEST_CASE ("the panel opens on the classic face", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getWaveBox().isVisible());
    REQUIRE (! h.section.getTableBox().isVisible());
    REQUIRE (! h.section.getPositionKnob().isVisible());
}

TEST_CASE ("choosing the wavetable mode swaps the face and the height", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    const auto classicHeight = h.section.getRequiredHeight();

    h.choose (h.section.getModeBox(), "Wavetable");

    REQUIRE (h.slot (0)[ids::mode].toString() == "wavetable");
    REQUIRE (h.section.isShowingWavetable());

    // The wave picker is gone and the wavetable's own controls are up.
    REQUIRE (! h.section.getWaveBox().isVisible());
    REQUIRE (h.section.getTableBox().isVisible());
    REQUIRE (h.section.getSourceBox().isVisible());
    REQUIRE (h.section.getPositionKnob().isVisible());
    REQUIRE (h.section.getUnisonKnob().isVisible());

    // And the host was told, because the mode lives on a node it does not
    // listen to - without this the new rows would land past the bottom edge.
    REQUIRE (h.section.getRequiredHeight() > classicHeight);
    REQUIRE (h.heightChanges > 0);

    REQUIRE (OscillatorSection::heightFor (true) > OscillatorSection::heightFor (false));
}

TEST_CASE ("switching the mode is one undo step, and undoing restores the face", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.choose (h.section.getModeBox(), "Wavetable");

    REQUIRE (h.document.getUndoManager().canUndo());

    h.document.getUndoManager().undo();

    REQUIRE (h.slot (0)[ids::mode].toString() == "classic");
    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getWaveBox().isVisible());
}

TEST_CASE ("the wavetable controls write to the slot they are showing", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.select (1);
    h.choose (h.section.getModeBox(), "Wavetable");
    h.choose (h.section.getTableBox(), wavetableAt (2).getDisplayName());
    h.choose (h.section.getSourceBox(), "LFO");

    h.section.getPositionKnob().setValue (0.75, juce::dontSendNotification);
    h.section.getPositionKnob().onValueChange();

    h.section.getUnisonKnob().setValue (4.0, juce::dontSendNotification);
    h.section.getUnisonKnob().onValueChange();

    REQUIRE (h.slot (1)[ids::wavetable].toString() == wavetableAt (2).getName());
    REQUIRE (h.slot (1)[ids::wavePositionSource].toString() == "lfo");
    REQUIRE ((double) h.slot (1)[ids::wavePosition] == Approx (0.75));

    // Written as an int, so the file keeps the schema default's type.
    REQUIRE (h.slot (1)[ids::unisonVoices].isInt());
    REQUIRE ((int) h.slot (1)[ids::unisonVoices] == 4);

    // The slot that is not showing is untouched.
    REQUIRE (h.slot (0)[ids::mode].toString() == "classic");
}

TEST_CASE ("selecting a slot in the other mode moves the height with it", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.slot (1).setProperty (ids::mode, "wavetable", nullptr);

    // Slot 0 is still classic and still showing, so nothing has changed yet.
    // Also the control case: it proves the pump below is what moved things,
    // rather than the section having been in the wavetable face all along.
    REQUIRE (! h.section.isShowingWavetable());

    h.select (1);

    REQUIRE (h.section.isShowingWavetable());
    REQUIRE (h.section.getRequiredHeight() == OscillatorSection::heightFor (true));
    REQUIRE (h.heightChanges > 0);

    h.select (0);

    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getRequiredHeight() == OscillatorSection::heightFor (false));
}

TEST_CASE ("the wavetable face paints its shape display", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    const auto classicInk = fractionOfNonBackgroundPixels (renderToImage (h.section));

    h.choose (h.section.getModeBox(), "Wavetable");

    REQUIRE (fractionOfNonBackgroundPixels (renderToImage (h.section)) > 0.05f);

    // The display is drawn from the table, so moving the position has to
    // change what is on screen - a shape that never moved would be decoration.
    const auto atStart = renderToImage (h.section);

    h.section.getPositionKnob().setValue (1.0, juce::dontSendNotification);
    h.section.getPositionKnob().onValueChange();

    const auto atEnd = renderToImage (h.section);

    bool differs = false;

    for (int y = 0; y < atStart.getHeight() && ! differs; ++y)
        for (int x = 0; x < atStart.getWidth() && ! differs; ++x)
            differs = atStart.getPixelAt (x, y) != atEnd.getPixelAt (x, y);

    REQUIRE (differs);
    REQUIRE (classicInk > 0.0f);
}

TEST_CASE ("the section survives having no channel in either mode", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.choose (h.section.getModeBox(), "Wavetable");
    h.section.setOwner ({});
    h.section.resized();

    REQUIRE (! h.section.isShowingWavetable());
    REQUIRE (h.section.getRequiredHeight() == OscillatorSection::heightFor (false));
    REQUIRE (renderToImage (h.section).isValid());
}

TEST_CASE ("the wavetable face fits the narrowest panel the app allows", "[ui][wavetable]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PanelHarness h;

    h.choose (h.section.getModeBox(), "Wavetable");

    // Settings::minPanelWidth is 220, and InstrumentPanel insets by 10 a side.
    // Three knobs share a row at that width, which is the tightest thing here.
    h.section.setSize (200, h.section.getRequiredHeight());
    h.section.resized();

    const std::initializer_list<juce::Component*> controls {
        &h.section.getTableBox(),  &h.section.getSourceBox(), &h.section.getPositionKnob(),
        &h.section.getModKnob(),   &h.section.getRateKnob(),  &h.section.getUnisonKnob(),
        &h.section.getSpreadKnob()
    };

    for (auto* control : controls)
    {
        const auto bounds = control->getBounds();

        INFO ("bounds " << bounds.toString());
        REQUIRE (bounds.getWidth() > 0);
        REQUIRE (bounds.getHeight() > 0);

        // Inside the section, not spilling off either edge or past the bottom.
        REQUIRE (h.section.getLocalBounds().contains (bounds));
    }

    // The three-knob row really is three across, not stacked or overlapping.
    REQUIRE (h.section.getPositionKnob().getRight() <= h.section.getModKnob().getX());
    REQUIRE (h.section.getModKnob().getRight() <= h.section.getRateKnob().getX());

    REQUIRE (fractionOfNonBackgroundPixels (renderToImage (h.section)) > 0.05f);
}
