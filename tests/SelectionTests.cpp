#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ControlWalkHarness.h"
#include "TestSupport.h"
#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/design/Tokens.h"
#include "ui/EffectChainComponent.h"
#include "ui/MixerComponent.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"
#include "ConfirmSupport.h"
#include "FixtureProject.h"

using namespace dew;
using namespace dew::testing;

namespace
{

void collect (juce::Component& root, juce::Array<juce::Component*>& out)
{
    for (auto* child : root.getChildren())
    {
        out.add (child);
        collect (*child, out);
    }
}

template <typename ComponentType> juce::Array<ComponentType*> findAll (juce::Component& root)
{
    juce::Array<juce::Component*> all;
    collect (root, all);

    juce::Array<ComponentType*> matches;

    for (auto* component : all)
        if (auto* typed = dynamic_cast<ComponentType*> (component))
            matches.add (typed);

    return matches;
}

/** Walks a grid of points over a row and reports every one where the component
    under the pointer is something that would swallow the click.

    This is the invariant that actually broke: a row's own mouseDown can be
    perfect and still never run, because a Label sitting on top of it consumes
    the press. `allowed` names the children that are *supposed* to take clicks.
*/
juce::Array<juce::Point<int>> deadSpots (juce::Component& row,
                                         const juce::Array<juce::Component*>& allowed)
{
    juce::Array<juce::Point<int>> dead;

    for (int y = 2; y < row.getHeight() - 2; y += 4)
        for (int x = 2; x < row.getWidth() - 2; x += 4)
        {
            const juce::Point<int> point { x, y };
            auto* hit = row.getComponentAt (point);

            if (hit == &row || hit == nullptr)
                continue;

            bool permitted = false;

            for (auto* candidate : allowed)
                for (auto* c = hit; c != nullptr; c = c->getParentComponent())
                    permitted = permitted || c == candidate;

            if (! permitted)
                dead.add (point);
        }

    return dead;
}

} // namespace

TEST_CASE ("a channel header has no click-swallowing dead zones", "[ui][selection]")
{
    // The name label covered the row's left half. juce::Component intercepts
    // clicks by default and Label::setEditable does not change that, so
    // clicking a channel by its name selected nothing at all.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    ChannelRackComponent rack { document, engine, editorState };
    rack.setSize (1000, 600);
    rack.setVisible (true);
    rack.refresh();
    rack.resized();

    const auto buttons = findAll<juce::Button> (rack);
    REQUIRE (buttons.size() >= 8); // M and S per demo channel, plus the footer

    // By id. Counting the widgets on a row identified it only for as long as
    // every row had exactly one label and two buttons, and an audio channel's
    // arm toggle is a third.
    juce::Array<juce::Component*> headers;
    juce::Array<juce::Component*> all;
    collect (rack, all);

    for (auto* component : all)
        if (component->getComponentID() == "channelHeader")
            headers.add (component);

    REQUIRE (headers.size() >= 4);

    for (auto* header : headers)
    {
        // M/S, the volume and pan knobs and the two number fields are meant to
        // take their own clicks; they select the row explicitly instead. A
        // field has to keep its press because the press is the start of a drag
        // - the same argument the knobs have - and it answers it by calling
        // onEditStart, which the case below drives.
        juce::Array<juce::Component*> allowed;

        for (auto* button : findAll<juce::Button> (*header))
            allowed.add (button);

        for (auto* slider : findAll<juce::Slider> (*header))
            allowed.add (slider);

        for (auto* field : findAll<DewNumberField> (*header))
            allowed.add (field);

        const auto dead = deadSpots (*header, allowed);
        INFO ("dead spots on a channel header: "
              << dead.size()
              << (dead.isEmpty() ? "" : juce::String (" first at ") + dead[0].toString()));
        REQUIRE (dead.isEmpty());
    }
}

TEST_CASE ("a channel row's knobs select their channel", "[ui][selection]")
{
    // Same rule as a mixer fader: a knob has to move when dragged, so it cannot
    // give its click away, and the row's own mouseDown never sees it. Selection
    // goes through the knob's own callback, which is the part a headless test
    // can verify.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    ChannelRackComponent rack { document, engine, editorState };
    rack.setSize (1000, 600);
    rack.setVisible (true);
    rack.refresh();
    rack.resized();

    const auto knobs = findAll<juce::Slider> (rack);

    // Two per channel, and the demo has at least four.
    REQUIRE (knobs.size() >= 8);

    for (auto* knob : knobs)
    {
        REQUIRE (knob->onDragStart != nullptr);

        editorState.setSelectedChannelId (-1);
        knob->onDragStart();

        REQUIRE (editorState.getSelectedChannelId() != -1);
    }
}

TEST_CASE ("a channel row's number fields select their channel", "[ui][selection]")
{
    // The other half of the exemption above. Base pitch and the mixer track are
    // DewNumberFields, so they are neither Buttons nor Sliders and the two
    // walks that cover the rest of the row miss them entirely: without this,
    // moving them here from the instrument panel would have added two controls
    // that swallow a press and select nothing.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    ChannelRackComponent rack { document, engine, editorState };
    rack.setSize (1000, 600);
    rack.setVisible (true);
    rack.refresh();
    rack.resized();

    const auto fields = findAll<DewNumberField> (rack);

    // Two per channel, and the demo has at least four.
    REQUIRE (fields.size() >= 8);

    for (auto* field : fields)
    {
        REQUIRE (field->onEditStart != nullptr);

        editorState.setSelectedChannelId (-1);
        field->onEditStart();

        REQUIRE (editorState.getSelectedChannelId() != -1);
    }
}

TEST_CASE ("a mixer strip has no click-swallowing dead zones", "[ui][selection]")
{
    // The fader took the strip's whole remaining height, so selection was only
    // reachable through the 6px border.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1000, 600);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    juce::Array<juce::Component*> all;
    collect (mixer, all);

    juce::Array<juce::Component*> strips;

    // Strips live inside a scrolling holder now, not directly under the mixer.
    // Each owns exactly one vertical fader, so find them through that.
    juce::ignoreUnused (all);

    for (auto* slider : findAll<juce::Slider> (mixer))
        if (slider->getSliderStyle() == juce::Slider::LinearVertical)
            strips.addIfNotAlreadyThere (slider->getParentComponent());

    REQUIRE (strips.size() >= 4);

    for (auto* strip : strips)
    {
        // Faders, the pan knob and M/S are meant to take their own clicks; they
        // select the strip explicitly instead.
        juce::Array<juce::Component*> allowed;

        for (auto* slider : findAll<juce::Slider> (*strip))
            allowed.add (slider);

        for (auto* button : findAll<juce::Button> (*strip))
            allowed.add (button);

        const auto dead = deadSpots (*strip, allowed);
        INFO ("dead spots on a mixer strip: "
              << dead.size()
              << (dead.isEmpty() ? "" : juce::String (" first at ") + dead[0].toString()));
        REQUIRE (dead.isEmpty());
    }
}

TEST_CASE ("hovering a channel's indicator does not select it", "[ui][selection]")
{
    // These selected their row from onStateChange, which fires for every
    // internal transition - buttonNormal -> buttonOver among them. The row also
    // forwards its children's mouse events so it can light up on hover, so
    // moving the pointer across the M and S buttons walked the selection down
    // the list with no click at all.
    //
    // Hover itself cannot be driven headlessly: mouseEnter arrives through a
    // ComponentPeer. What can be checked is that nothing is wired to a state
    // change in the first place, which is the mechanism that made hover matter.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    ChannelRackComponent rack { document, engine, editorState };
    rack.setSize (1000, 600);
    rack.setVisible (true);
    rack.resized();

    // One indicator per channel now, where there were an M and an S - and it
    // is a DewIconButton, so the sweep looks for the arm button's type rather
    // than the letter toggle's. Filtered by component ID, because an arm button
    // is the same type and is deliberately wired the other way.
    juce::Array<DewIconButton*> toggles;

    for (auto* button : findAll<DewIconButton> (rack))
        if (button->getComponentID() == "channelEnabled")
            toggles.add (button);

    REQUIRE (toggles.size() >= 4); // one on every channel

    for (auto* toggle : toggles)
    {
        INFO ("toggle: " << toggle->getName());
        CHECK (toggle->onStateChange == nullptr);

        // onModifiedClick rather than onClick: shift is part of the gesture -
        // it says the same thing of every channel - and juce::Button::onClick
        // does not carry the modifiers.
        CHECK (toggle->onModifiedClick != nullptr);
    }

    // The control case: a real click still selects, so this has not passed by
    // taking the selection away from the indicator altogether.
    editorState.setSelectedChannelId (-1);
    toggles.getFirst()->onModifiedClick ({});

    CHECK (editorState.getSelectedChannelId() != -1);
}

TEST_CASE ("controls that keep their own clicks still select their row", "[ui][selection]")
{
    // A fader has to move when dragged, so it cannot give its click away. It
    // selects the strip through its own callback instead - which is the part
    // that can be verified without a window peer.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1000, 600);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    const auto faders = findAll<juce::Slider> (mixer);
    REQUIRE (faders.size() >= 4);

    int selectionsSeen = 0;

    for (auto* fader : faders)
    {
        REQUIRE (fader->onDragStart != nullptr);

        editorState.setSelectedMixerTrackId (-1);
        fader->onDragStart();

        if (editorState.getSelectedMixerTrackId() != -1)
            ++selectionsSeen;
    }

    // Every fader and pan knob in the mixer selects its own strip.
    REQUIRE (selectionsSeen == faders.size());
}

TEST_CASE ("the master strip is selectable, like every other", "[ui][selection]")
{
    // It had no onSelected at all, so clicking it did nothing and its effect
    // chain could not be reached.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1000, 600);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    juce::Array<int> selectedIds;

    for (auto* fader : findAll<juce::Slider> (mixer))
    {
        editorState.setSelectedMixerTrackId (-1);
        fader->onDragStart();
        selectedIds.addIfNotAlreadyThere (editorState.getSelectedMixerTrackId());
    }

    // Four inserts plus the master, which answers to its own reserved id.
    REQUIRE (selectedIds.contains (MixerComponent::masterTrackId));
    REQUIRE (selectedIds.size() >= 5);
}

TEST_CASE ("an effect row's buttons select it before acting", "[ui][selection]")
{
    // Bypassing slot three while the parameters below still showed slot one is
    // the kind of thing that reads as the editor ignoring you.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    EffectChainComponent chain { document, editorState };
    chain.setSize (320, 400);
    chain.setVisible (true);
    chain.setOwner (document.getState().getChildWithName (ids::CHANNEL));

    chain.addEffectOfType ("filter");
    chain.addEffectOfType ("delay");
    chain.addEffectOfType ("reverb");

    REQUIRE (chain.getNumSlotRows() == 3);

    chain.selectSlot (0);
    REQUIRE (chain.getSelectedSlot() == 0);

    // The bypass toggle on the last row.
    juce::Array<juce::Component*> all;
    collect (chain, all);

    juce::Array<juce::Component*> rows;

    for (auto* component : all)
        if (component->getParentComponent() == &chain
            && findAll<juce::Button> (*component).size() >= 4)
            rows.add (component);

    REQUIRE (rows.size() == 3);

    auto lastRowButtons = findAll<juce::Button> (*rows.getLast());
    REQUIRE (lastRowButtons.size() >= 4);

    // Invoked directly rather than through triggerClick, which posts to a
    // message loop a console test does not run.
    REQUIRE (lastRowButtons.getFirst()->onClick != nullptr);
    lastRowButtons.getFirst()->onClick();

    REQUIRE (chain.getSelectedSlot() == 2);
}

// --- the channel rack's row actions ------------------------------------------

TEST_CASE ("the add-channel button is the row after the last channel", "[ui][rack]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    ChannelRackComponent rack { document, engine, editorState };

    rack.setSize (1200, 600);
    rack.setVisible (true);
    rack.resized();

    auto* button = findDescendantWithID (rack, "addChannelButton");
    REQUIRE (button != nullptr);

    // Inside the scrolling holder, not on the panel: it is a row of the list, so
    // it scrolls with the list rather than floating over it in a footer.
    REQUIRE (button->getParentComponent() != &rack);
    REQUIRE (button->getParentComponent()->getComponentID() == "channelRackContent");

    // In the header column, and below every channel row.
    REQUIRE (button->getX() >= 0);
    REQUIRE (button->getRight() <= tokens::size::gutterChannel);
    REQUIRE (button->getY() >= 4 * tokens::size::rowHeight);
    REQUIRE (button->getY() < 5 * tokens::size::rowHeight);
}

TEST_CASE ("a channel row's context menu offers rename, add and remove", "[ui][rack]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    ChannelRackComponent rack { document, engine, editorState };

    rack.setSize (1200, 600);
    rack.setVisible (true);
    rack.resized();

    const auto items = rack.channelMenuItems (1);

    INFO ("items: " << items.joinIntoString (", "));
    REQUIRE (items.contains ("Rename"));
    REQUIRE (items.contains ("Add channel"));
    REQUIRE (items.contains ("Remove channel"));

    // Removing is separated from the two that build, so the destructive item is
    // not adjacent to the one directly above it in muscle memory.
    REQUIRE (items.indexOf ("-") == items.indexOf ("Remove channel") - 1);

    REQUIRE_FALSE (rack.applyChannelMenuChoice (999, 1));
}

TEST_CASE ("removing a channel from its own row removes that channel", "[ui][rack]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    ChannelRackComponent rack { document, engine, editorState };

    // This test is about which ROW the menu acts on, not about the question it
    // now asks first, so it answers yes at once.
    rack.confirmDestructive = testing::alwaysConfirm();

    rack.setSize (1200, 600);
    rack.setVisible (true);
    rack.resized();

    const auto countChannels = [&document]
    {
        int n = 0;

        for (const auto& channel : document.getState())
            if (channel.hasType (ids::CHANNEL))
                ++n;

        return n;
    };

    REQUIRE (countChannels() == 4);

    // The row acts on ITSELF, not on whatever happens to be selected - which is
    // what the footer button did, from the far end of the panel.
    editorState.setSelectedChannelId (1);
    REQUIRE (rack.applyChannelMenuChoice (3, (int) 3));

    REQUIRE (countChannels() == 3);
    REQUIRE_FALSE (ProjectEdits::findChannel (document.getState(), 3).isValid());
    REQUIRE (ProjectEdits::findChannel (document.getState(), 1).isValid());
}

TEST_CASE ("right-clicking a channel row selects it", "[ui][rack]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    ChannelRackComponent rack { document, engine, editorState };

    rack.setSize (1200, 600);
    rack.setVisible (true);
    rack.resized();

    editorState.setSelectedChannelId (1);

    // Second row. Selecting on the way into the menu is what makes the menu act
    // on the row that was aimed at rather than on the previous selection.
    auto* holder = findDescendantWithID (rack, "channelRackContent");
    REQUIRE (holder != nullptr);

    juce::Component* secondRow = nullptr;

    for (auto* child : holder->getChildren())
        if (child->getY() == tokens::size::rowHeight
            && child->getWidth() == tokens::size::gutterChannel)
            secondRow = child;

    REQUIRE (secondRow != nullptr);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };
    const juce::Point<float> at { 20.0f, (float) (tokens::size::rowHeight / 2) };

    secondRow->mouseDown (mouseEventAt (*secondRow, at, rightButton));

    REQUIRE (editorState.getSelectedChannelId() == 2);
}
