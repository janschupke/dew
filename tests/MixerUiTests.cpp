// The strips: what they show, how they scroll, and what their menus do.
//
// Split out of MixerTests.cpp along its tags; the UI group kept the fixture
// that sat inside it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/MixerComponent.h"
#include "ui/design/Tokens.h"

#include "ConfirmSupport.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

TEST_CASE ("each strip lists the channels routed into it", "[mixer][ui]")
{
    // Without this the mixer is a row of anonymous faders with nothing saying
    // what any of them carries.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (dew::testing::fixtureProject(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1000, 700);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    juce::Image image (juce::Image::ARGB, mixer.getWidth(), mixer.getHeight(), true);
    juce::Graphics g (image);
    mixer.paintEntireComponent (g, true);

    // Each demo channel's colour appears somewhere in the mixer, as its routing
    // dot on the insert it feeds.
    for (const auto& channel : document.getState())
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        const auto target = juce::Colour::fromString (
            "ff" + channel[ids::colour].toString().getLastCharacters (6));

        int matching = 0;

        for (int y = 0; y < image.getHeight(); y += 1)
            for (int x = 0; x < image.getWidth(); x += 1)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (std::abs ((int) pixel.getRed() - (int) target.getRed()) < 20
                    && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) < 20
                    && std::abs ((int) pixel.getBlue() - (int) target.getBlue()) < 20)
                    ++matching;
            }

        INFO ("channel " << channel[ids::name].toString() << " dot pixels: " << matching);
        REQUIRE (matching > 0);
    }
}

TEST_CASE ("strips scroll rather than vanishing when there are many", "[mixer][ui]")
{
    // removeFromLeft on a fixed rectangle clamps at the right edge, so past
    // about twelve inserts every further strip - including the master, added
    // last - was silently given no width.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    juce::UndoManager& undo = document.getUndoManager();
    auto mixerTree = document.getState().getChildWithName (ids::MIXER);

    for (int i = 5; i <= 24; ++i)
    {
        juce::ValueTree track (ids::MIXER_TRACK);
        track.setProperty (ids::id, i, nullptr);
        track.setProperty (ids::name, "Insert " + juce::String (i), nullptr);
        track.setProperty (ids::gain, 0.8, nullptr);
        mixerTree.appendChild (track, &undo);
    }

    MixerComponent mixer { document, editorState };
    mixer.setSize (900, 600);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    juce::Array<juce::Component*> faders;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* slider = dynamic_cast<juce::Slider*> (child);
                slider != nullptr && slider->getSliderStyle() == juce::Slider::LinearVertical)
                faders.add (slider->getParentComponent());

            walk (*child);
        }
    };

    walk (mixer);

    // Twenty-four inserts plus the master, and every one of them has real width.
    REQUIRE (faders.size() == 25);

    for (auto* strip : faders)
    {
        INFO ("strip width " << strip->getWidth());
        REQUIRE (strip->getWidth() > 20);
    }
}

TEST_CASE ("the mixer is two rows: strips above, the effect chain below", "[mixer][ui]")
{
    // The chain used to be capped at 430px wide in a panel over a thousand
    // wide, with a column of cards scrolling inside it. It is a full-width row
    // now, and it must not eat into the strips or hang off the bottom.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1200, 700);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    auto* chainHost = mixer.findChildWithID ("effectChainHost");
    REQUIRE (chainHost != nullptr);

    juce::Component* stripArea = nullptr;

    for (auto* child : mixer.getChildren())
        if (dynamic_cast<juce::Viewport*> (child) != nullptr)
            stripArea = child;

    REQUIRE (stripArea != nullptr);

    INFO ("strips " << stripArea->getBounds().toString() << " chain "
                    << chainHost->getBounds().toString());

    // Two rows, in that order, neither overlapping the other.
    REQUIRE (chainHost->getY() >= stripArea->getBottom());
    REQUIRE (chainHost->getBottom() <= mixer.getHeight());

    // The row spans the mixer rather than a fixed slot at one end of it.
    REQUIRE (chainHost->getWidth() > mixer.getWidth() * 3 / 4);

    // And the strips still get the larger share.
    REQUIRE (stripArea->getHeight() > chainHost->getHeight());
}

TEST_CASE ("the effect chain sits on a container with an edge", "[mixer][ui]")
{
    // The chain painted a heading and nothing else, and the mixer under it was
    // a bare fillAll - so the effect row floated on the window background with
    // nothing to say where it began or that it belonged to the selected strip.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;
    document.setState (ProjectFactory::createDefault(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1200, 700);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    auto* chainHost = mixer.findChildWithID ("effectChainHost");
    REQUIRE (chainHost != nullptr);

    juce::Image image (juce::Image::ARGB, mixer.getWidth(), mixer.getHeight(), true);
    juce::Graphics g (image);
    mixer.paintEntireComponent (g, true);

    const auto bounds = chainHost->getBounds();

    // Just outside the card is the mixer's background; just inside is a
    // surface. If the two are the same colour there is no container.
    const auto outside = image.getPixelAt (bounds.getCentreX(), bounds.getY() - 3);
    const auto inside = image.getPixelAt (bounds.getCentreX(), bounds.getY() + 4);

    INFO ("outside " << outside.toString() << " inside " << inside.toString());
    CHECK (outside != inside);

    // And an edge along the top of it, brighter than either.
    auto foundEdge = false;

    for (int y = bounds.getY(); y < bounds.getY() + 3 && ! foundEdge; ++y)
    {
        const auto pixel = image.getPixelAt (bounds.getCentreX(), y);
        foundEdge = pixel.getBrightness() > inside.getBrightness() + 0.02f;
    }

    CHECK (foundEdge);
}

TEST_CASE ("dragging a mixer fader is one undo step", "[ui][mixer]")
{
    // beginNewTransaction arms a new transaction rather than being a no-op when
    // one is open, so calling it per value change made every pixel of a drag its
    // own undo step - on the fader whose whole point is being dragged.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    MixerComponent mixer { document, editorState };
    mixer.setSize (1000, 600);
    mixer.setVisible (true);
    mixer.refresh();
    mixer.resized();

    juce::Slider* fader = nullptr;
    juce::Slider* pan = nullptr;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                if (fader == nullptr && slider->getSliderStyle() == juce::Slider::LinearVertical)
                    fader = slider;
                else if (pan == nullptr && slider->getRange().getStart() < 0.0)
                    pan = slider;
            }

            walk (*child);
        }
    };

    walk (mixer);

    REQUIRE (fader != nullptr);
    REQUIRE (pan != nullptr);

    // A gesture needs an end as much as a start: without one the transaction
    // opened at onDragStart is never released and every later change joins it.
    REQUIRE (fader->onDragStart != nullptr);
    REQUIRE (fader->onDragEnd != nullptr);
    REQUIRE (pan->onDragEnd != nullptr);

    auto track = ProjectEdits::findMixerTrack (document.getState(), 1);
    REQUIRE (track.isValid());

    const auto gainBefore = (double) track[ids::gain];

    fader->onDragStart();

    for (int i = 1; i <= 20; ++i)
        fader->setValue ((double) i / 40.0, juce::sendNotificationSync);

    if (fader->onDragEnd != nullptr)
        fader->onDragEnd();

    REQUIRE ((double) track[ids::gain] == Approx (0.5));

    document.getUndoManager().undo();

    REQUIRE ((double) track[ids::gain] == Approx (gainBefore));

    // and the same for the pan knob beside it
    const auto panBefore = (double) track[ids::pan];

    pan->onDragStart();

    for (int i = 1; i <= 20; ++i)
        pan->setValue ((double) -i / 40.0, juce::sendNotificationSync);

    if (pan->onDragEnd != nullptr)
        pan->onDragEnd();

    REQUIRE ((double) track[ids::pan] == Approx (-0.5));

    document.getUndoManager().undo();

    REQUIRE ((double) track[ids::pan] == Approx (panBefore));
}

namespace
{

/** Everything a mixer needs, laid out, with the confirmation answered at once -
    what these tests are about is the edit, not the asking. */
struct MixerHarness
{
    MixerHarness()
    {
        document.setState (ProjectFactory::createDefault(), true);
        mixer.confirmDestructive = dew::testing::alwaysConfirm();
        mixer.setSize (1000, 700);
        mixer.setVisible (true);
        mixer.refresh();
        mixer.resized();
    }

    ProjectDocument document;
    EditorState editorState;
    MixerComponent mixer { document, editorState };
};

/** Component::findChildWithID is NOT recursive, and the strips and the add
    button live inside the viewport's holder. */
juce::Component* findDescendantWithID (juce::Component& root, const juce::String& id)
{
    for (auto* child : root.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findDescendantWithID (*child, id))
            return found;
    }

    return nullptr;
}

constexpr int addInsertChoice = 2;
constexpr int removeInsertChoice = 3;

} // namespace

TEST_CASE ("a strip's menu offers rename, add and remove", "[mixer][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    const auto items = h.mixer.mixerTrackMenuItems (1);

    CHECK (items.contains ("Rename"));
    CHECK (items.contains ("Add insert"));
    CHECK (items.contains ("Remove insert"));

    // The separator sits immediately above the destructive item, the way the
    // rack's and the playlist's menus do.
    CHECK (items.indexOf ("-") == items.indexOf ("Remove insert") - 1);

    CHECK (h.mixer.mixerTrackMenuItems (999).isEmpty());
    CHECK_FALSE (h.mixer.applyMixerTrackMenuChoice (999, addInsertChoice));
}

TEST_CASE ("the master strip's menu builds but does not destroy", "[mixer][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    // The master carries no id property, so it reads as 0 - which is exactly
    // MixerComponent::masterTrackId, and is why the id-keyed seam reaches it.
    const auto items = h.mixer.mixerTrackMenuItems (MixerComponent::masterTrackId);

    CHECK (items.contains ("Add insert"));
    CHECK_FALSE (items.contains ("Rename"));
    CHECK_FALSE (items.contains ("Remove insert"));
}

TEST_CASE ("an insert can be added and removed from its own strip", "[mixer][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    const auto before = ProjectEdits::countMixerTracks (h.document.getState());
    const auto stripsBefore = h.mixer.getNumStrips();
    REQUIRE (before == 4);

    REQUIRE (h.mixer.applyMixerTrackMenuChoice (1, addInsertChoice));
    CHECK (ProjectEdits::countMixerTracks (h.document.getState()) == before + 1);
    CHECK (h.mixer.getNumStrips() == stripsBefore + 1);

    // Removing rebuilds the strips synchronously, which deletes the Strip whose
    // applyMenuChoice is still on the stack. If the id were read off a member
    // after the callback this would be a use-after-free, so the test drives it
    // and then asks the mixer to paint.
    REQUIRE (h.mixer.applyMixerTrackMenuChoice (3, removeInsertChoice));
    CHECK (ProjectEdits::countMixerTracks (h.document.getState()) == before);
    CHECK_FALSE (ProjectEdits::findMixerTrack (h.document.getState(), 3).isValid());

    juce::Image image (juce::Image::ARGB, h.mixer.getWidth(), h.mixer.getHeight(), true);
    juce::Graphics g (image);
    h.mixer.paintEntireComponent (g, true);
}

TEST_CASE ("removing the selected insert leaves the chain pointed somewhere", "[mixer][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    h.editorState.setSelectedMixerTrackId (3);
    h.editorState.dispatchPendingMessages();
    REQUIRE (h.editorState.getSelectedMixerTrackId() == 3);

    REQUIRE (h.mixer.applyMixerTrackMenuChoice (3, removeInsertChoice));
    h.editorState.dispatchPendingMessages();

    // Session state, fixed up by the mixer rather than by the edit - it must not
    // be on the undo stack.
    CHECK (h.editorState.getSelectedMixerTrackId() == MixerComponent::masterTrackId);
}

TEST_CASE ("the strip row ends in a way to add one", "[mixer][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    auto* add = findDescendantWithID (h.mixer, "addMixerTrack");
    REQUIRE (add != nullptr);

    auto* button = dynamic_cast<juce::Button*> (add);
    REQUIRE (button != nullptr);

    // It sits past the last strip rather than over it, or it would be an add
    // button you cannot reach without covering an insert.
    auto lastStripRight = 0;

    for (auto* child : add->getParentComponent()->getChildren())
        if (child != add)
            lastStripRight = juce::jmax (lastStripRight, child->getRight());

    CHECK (add->getX() >= lastStripRight);

    const auto before = ProjectEdits::countMixerTracks (h.document.getState());
    button->onClick();
    CHECK (ProjectEdits::countMixerTracks (h.document.getState()) == before + 1);
}

TEST_CASE ("a strip is one column wide and still holds its controls", "[mixer][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MixerHarness h;

    auto* add = findDescendantWithID (h.mixer, "addMixerTrack");
    REQUIRE (add != nullptr);

    auto* holder = add->getParentComponent();
    auto strips = 0;

    for (auto* strip : holder->getChildren())
    {
        if (strip == add)
            continue;

        ++strips;
        CHECK (strip->getWidth() == tokens::size::mixerStripWidth);

        // Narrowing a strip until a control collapses is the failure this
        // catches: every child has to have real bounds inside its strip.
        for (auto* control : strip->getChildren())
        {
            INFO ("control " << control->getComponentID() << " in a strip");
            CHECK (control->getWidth() > 0);
            CHECK (control->getHeight() > 0);
            CHECK (strip->getLocalBounds().contains (control->getBounds()));
        }
    }

    CHECK (strips == h.mixer.getNumStrips());
}
