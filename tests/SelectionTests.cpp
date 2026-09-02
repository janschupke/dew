#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/EffectChainComponent.h"
#include "ui/MixerComponent.h"

using namespace dew;

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

template <typename ComponentType>
juce::Array<ComponentType*> findAll (juce::Component& root)
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

    document.setState (ProjectFactory::createDemo(), true);

    ChannelRackComponent rack { document, engine, editorState };
    rack.setSize (1000, 600);
    rack.setVisible (true);
    rack.refresh();
    rack.resized();

    const auto buttons = findAll<juce::Button> (rack);
    REQUIRE (buttons.size() >= 8);      // M and S per demo channel, plus the footer

    // The header rows are the components holding a Label plus two Buttons.
    juce::Array<juce::Component*> headers;
    juce::Array<juce::Component*> all;
    collect (rack, all);

    for (auto* component : all)
        if (findAll<juce::Label> (*component).size() == 1
            && findAll<juce::Button> (*component).size() == 2)
            headers.add (component);

    REQUIRE (headers.size() >= 4);

    for (auto* header : headers)
    {
        juce::Array<juce::Component*> allowed;

        for (auto* button : findAll<juce::Button> (*header))
            allowed.add (button);

        const auto dead = deadSpots (*header, allowed);
        INFO ("dead spots on a channel header: " << dead.size()
              << (dead.isEmpty() ? "" : juce::String (" first at ") + dead[0].toString()));
        REQUIRE (dead.isEmpty());
    }
}

TEST_CASE ("a mixer strip has no click-swallowing dead zones", "[ui][selection]")
{
    // The fader took the strip's whole remaining height, so selection was only
    // reachable through the 6px border.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (ProjectFactory::createDemo(), true);

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
        INFO ("dead spots on a mixer strip: " << dead.size()
              << (dead.isEmpty() ? "" : juce::String (" first at ") + dead[0].toString()));
        REQUIRE (dead.isEmpty());
    }
}

TEST_CASE ("controls that keep their own clicks still select their row", "[ui][selection]")
{
    // A fader has to move when dragged, so it cannot give its click away. It
    // selects the strip through its own callback instead - which is the part
    // that can be verified without a window peer.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (ProjectFactory::createDemo(), true);

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

    document.setState (ProjectFactory::createDemo(), true);

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
