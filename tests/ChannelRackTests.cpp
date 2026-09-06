#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/InstrumentPanel.h"
#include "ui/design/Tokens.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

template <typename Type> void collectOfType (juce::Component& root, juce::Array<Type*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* match = dynamic_cast<Type*> (child))
            out.add (match);

        collectOfType (*child, out);
    }
}

/** The rack's rows, found the way the selection tests find them: a row is the
    component holding the name label and the M/S pair.
*/
juce::Array<juce::Component*> headersOf (juce::Component& rack)
{
    juce::Array<juce::Component*> all, headers;
    collectOfType<juce::Component> (rack, all);

    // By id, not by shape. Counting the widgets on a row identified it only for
    // as long as every row had exactly one label and two buttons, and an audio
    // channel's arm toggle is a third.
    for (auto* component : all)
        if (component->getComponentID() == "channelHeader")
            headers.add (component);

    return headers;
}

/** Volume and pan are told apart by their range rather than by child order:
    only pan reaches below zero. Which is also how a user tells them apart.
*/
juce::Slider* knobOf (juce::Component& header, bool wantPan)
{
    juce::Array<juce::Slider*> sliders;
    collectOfType (header, sliders);

    for (auto* slider : sliders)
        if ((slider->getRange().getStart() < 0.0) == wantPan)
            return slider;

    return nullptr;
}

struct Harness
{
    Harness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        rack.setSize (1000, 600);
        rack.setVisible (true);
        rack.refresh();
        rack.resized();
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    ChannelRackComponent rack { document, engine, editorState };
};

} // namespace

TEST_CASE ("a channel row's knobs write to that channel", "[ui][channelrack]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Harness h;

    const auto headers = headersOf (h.rack);
    REQUIRE (headers.size() >= 2);

    // The second row, so a write landing on the wrong channel is visible.
    auto* header = headers[1];

    auto* volume = knobOf (*header, false);
    auto* pan = knobOf (*header, true);

    REQUIRE (volume != nullptr);
    REQUIRE (pan != nullptr);

    juce::Array<juce::ValueTree> channels;

    for (const auto& child : h.document.getState())
        if (child.hasType (ids::CHANNEL))
            channels.add (child);

    REQUIRE (channels.size() == headers.size());

    auto channel = channels[1];
    auto other = channels[0];

    const auto otherVolumeBefore = (double) other[ids::volume];

    volume->setValue (0.42, juce::sendNotificationSync);
    pan->setValue (-0.75, juce::sendNotificationSync);

    REQUIRE ((double) channel[ids::volume] == Catch::Approx (0.42));
    REQUIRE ((double) channel[ids::pan] == Catch::Approx (-0.75));

    // and nowhere else
    REQUIRE ((double) other[ids::volume] == Catch::Approx (otherVolumeBefore));
}

TEST_CASE ("a channel row follows the channel it shows", "[ui][channelrack]")
{
    // The same value is editable here and in the instrument panel, so the two
    // have to agree - a change made in one must show up in the other.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Harness h;

    const auto headers = headersOf (h.rack);
    REQUIRE (! headers.isEmpty());

    auto channel = ProjectEdits::findChannel (h.document.getState(), (int) headers.size());
    REQUIRE (channel.isValid());

    auto* header = headers[headers.size() - 1];
    auto* volume = knobOf (*header, false);
    auto* pan = knobOf (*header, true);

    REQUIRE (volume != nullptr);
    REQUIRE (pan != nullptr);

    channel.setProperty (ids::volume, 0.17, nullptr);
    channel.setProperty (ids::pan, 0.63, nullptr);

    REQUIRE (volume->getValue() == Catch::Approx (0.17));
    REQUIRE (pan->getValue() == Catch::Approx (0.63));
}

TEST_CASE ("dragging a channel row's knob is one undo step", "[ui][channelrack]")
{
    // A drag produces a value change per pixel. Each one is a real edit, so
    // without a transaction that spans the gesture, undoing a fader move means
    // pressing undo several hundred times.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Harness h;

    const auto headers = headersOf (h.rack);
    REQUIRE (! headers.isEmpty());

    auto* volume = knobOf (*headers[0], false);
    REQUIRE (volume != nullptr);
    REQUIRE (volume->onDragStart != nullptr);

    // A gesture needs an end as much as a start: without one the transaction
    // opened at onDragStart is never released and every later change joins it.
    REQUIRE (volume->onDragEnd != nullptr);

    auto channel = ProjectEdits::findChannel (h.document.getState(), 1);
    REQUIRE (channel.isValid());

    const auto before = (double) channel[ids::volume];

    volume->onDragStart();

    for (int i = 1; i <= 20; ++i)
        volume->setValue ((double) i / 40.0, juce::sendNotificationSync);

    if (volume->onDragEnd != nullptr)
        volume->onDragEnd();

    REQUIRE ((double) channel[ids::volume] == Catch::Approx (0.5));

    h.document.getUndoManager().undo();

    REQUIRE ((double) channel[ids::volume] == Catch::Approx (before));
}

TEST_CASE ("the instrument panel's knobs are one undo step too", "[ui][channelrack]")
{
    // The same two channel properties, edited from the other surface. They have
    // to behave the same, and this half had the defect the rack's half was
    // written against: beginNewTransaction per value change.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);
    editorState.setSelectedChannelId (1);

    InstrumentPanel panel { document, editorState };
    panel.setSize (280, 900);
    panel.setVisible (true);
    panel.resized();

    juce::Array<juce::Slider*> sliders;
    collectOfType (panel, sliders);

    juce::Slider* volume = nullptr;

    // The panel's VOLUME knob: 0..1 with a fine interval. Pan starts below zero,
    // sustain lives on the AMP node, and base pitch is an inc/dec stepper.
    for (auto* slider : sliders)
        if (slider->isRotary() && slider->getRange() == juce::Range<double> (0.0, 1.0)
            && juce::exactlyEqual (slider->getInterval(), 0.001))
            volume = slider;

    REQUIRE (volume != nullptr);
    REQUIRE (volume->onDragStart != nullptr);
    REQUIRE (volume->onDragEnd != nullptr);

    auto channel = ProjectEdits::findChannel (document.getState(), 1);
    REQUIRE (channel.isValid());

    const auto before = (double) channel[ids::volume];

    volume->onDragStart();

    for (int i = 1; i <= 20; ++i)
        volume->setValue ((double) i / 40.0, juce::sendNotificationSync);

    volume->onDragEnd();

    REQUIRE ((double) channel[ids::volume] == Catch::Approx (0.5));

    document.getUndoManager().undo();

    REQUIRE ((double) channel[ids::volume] == Catch::Approx (before));
}

TEST_CASE ("a channel's instrument can be changed without losing the channel",
           "[ui][channelrack][instrument]")
{
    // `source` was written at exactly two sites, both inside add*Channel, so
    // picking the wrong kind meant deleting the channel and losing its notes,
    // its colour, its mixer routing and its place in the rack with it. The
    // panel that SHOWS the instrument could not change it at all.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);
    editorState.setSelectedChannelId (1);

    auto channel = ProjectEdits::findChannel (document.getState(), 1);
    REQUIRE (channel.isValid());
    REQUIRE (ProjectEdits::instrumentTypeOf (channel) == InstrumentType::synth);

    const auto name = channel[ids::name].toString();
    const auto colour = channel[ids::colour].toString();
    const auto routing = (int) channel[ids::mixerTrackId];

    REQUIRE (ProjectEdits::setInstrumentType (channel, InstrumentType::soundfont,
                                              &document.getUndoManager()));
    CHECK (ProjectEdits::instrumentTypeOf (channel) == InstrumentType::soundfont);

    // Everything that identifies the channel survives, because the swap is one
    // property write: the schema gives every channel an instrument, a sample
    // AND a soundfont node whichever kind it is, and `source` only says which
    // is live.
    CHECK (channel[ids::name].toString() == name);
    CHECK (channel[ids::colour].toString() == colour);
    CHECK ((int) channel[ids::mixerTrackId] == routing);
    CHECK (channel.getChildWithName (ids::INSTRUMENT).isValid());
    CHECK (channel.getChildWithName (ids::SOUNDFONT).isValid());

    // Non-destructive, and that is the design rather than an oversight: a synth
    // turned into something else keeps its notes, so swapping back - or one
    // undo - brings the sound straight back.
    document.getUndoManager().undo();
    CHECK (ProjectEdits::instrumentTypeOf (channel) == InstrumentType::synth);

    // Asking for the kind it already is changes nothing and starts no undo step.
    CHECK_FALSE (ProjectEdits::setInstrumentType (channel, InstrumentType::synth,
                                                  &document.getUndoManager()));
}

TEST_CASE ("the instrument band folds and the panel gets shorter", "[ui][channelrack][instrument]")
{
    // The tallest band in the sidebar, and the only one there that could not be
    // got out of the way - the effect cards had folded since they were written,
    // so reaching a long chain meant scrolling past a section you were not
    // using.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);
    editorState.setSelectedChannelId (1);

    InstrumentPanel panel { document, editorState };
    panel.setSize (280, 900);
    panel.setVisible (true);
    panel.resized();

    const auto open = panel.getRequiredHeight();

    editorState.setInstrumentExpanded (false);
    editorState.dispatchPendingMessages();
    panel.resized();

    const auto folded = panel.getRequiredHeight();

    INFO ("open " << open << ", folded " << folded);
    CHECK (folded < open);

    // The control case: folding must not collapse the panel to nothing. The
    // title and the effect chain below it stay, which is the whole point of
    // folding this band rather than hiding the panel.
    CHECK (folded > tokens::size::stripHeading);

    editorState.setInstrumentExpanded (true);
    editorState.dispatchPendingMessages();
    panel.resized();

    CHECK (panel.getRequiredHeight() == open);
}
