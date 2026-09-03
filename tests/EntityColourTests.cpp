#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/ColourMenu.h"
#include "ui/EditorState.h"
#include "ui/MixerComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/design/Tokens.h"
#include "PaintProbe.h"

using namespace dew;

namespace
{

juce::ValueTree firstTrack (ProjectDocument& document)
{
    for (const auto& track : document.getState().getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK))
            return track;

    return {};
}

juce::ValueTree firstStrip (ProjectDocument& document)
{
    for (const auto& strip : document.getState().getChildWithName (ids::MIXER))
        if (strip.hasType (ids::MIXER_TRACK))
            return strip;

    return {};
}

} // namespace

TEST_CASE ("an empty colour means inherit, and is not a colour", "[model][colour]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    auto track = firstTrack (document);
    REQUIRE (track.isValid());

    // THE distinction the schema default rests on. A lane and a strip already
    // look like something without choosing - a lane from its position, a strip
    // from what is routed into it - so the property has to be able to say
    // "nothing chosen" as well as "this one".
    CHECK_FALSE (entityColour::stored (track).has_value());

    // of() still answers, because a caller with nothing to fall back to needs a
    // colour rather than an optional.
    CHECK (entityColour::of (track).isOpaque());

    ProjectEdits::setColour (track, entityColour::defaultHex (3), nullptr);

    REQUIRE (entityColour::stored (track).has_value());
    CHECK (*entityColour::stored (track)
           == juce::Colour::fromString (entityColour::defaultHex (3)));

    // And clearing it goes back to inheriting rather than to ramp entry zero.
    ProjectEdits::setColour (track, {}, nullptr);
    CHECK_FALSE (entityColour::stored (track).has_value());
}

TEST_CASE ("a colour written by the menu is a colour the model reads back",
           "[model][colour]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // hexOf and defaultHex are two ways of spelling the same thing, and a
    // round trip through the pair is what says so.
    for (int i = 0; i < entityColour::rampSize(); ++i)
    {
        INFO ("ramp entry " << i << " (" << entityColour::rampName (i) << ")");

        const auto colour = juce::Colour::fromString (entityColour::defaultHex (i));

        CHECK (entityColour::hexOf (colour) == entityColour::defaultHex (i));
        CHECK (entityColour::rampName (i).isNotEmpty());
    }
}

TEST_CASE ("changing a colour is one undo step, on all three", "[model][colour][undo]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    auto& undo = document.getUndoManager();

    const juce::ValueTree nodes[] = {
        ProjectEdits::findChannel (document.getState(), 1),
        firstTrack (document),
        firstStrip (document),
    };

    for (const auto& node : nodes)
    {
        REQUIRE (node.isValid());

        const auto before = node[ids::colour].toString();

        undo.beginNewTransaction ("Change colour");
        ProjectEdits::setColour (node, entityColour::defaultHex (5), &undo);

        REQUIRE (node[ids::colour].toString() == entityColour::defaultHex (5));

        REQUIRE (undo.undo());
        CHECK (node[ids::colour].toString() == before);
    }
}

TEST_CASE ("every menu that offers a colour offers the same ones", "[ui][colour]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    auto track = firstTrack (document);
    REQUIRE (track.isValid());

    juce::PopupMenu menu;
    colourMenu::addTo (menu, track, 100);

    // MenuItemIterator keeps a REFERENCE, so the menu has to be a named local -
    // iterating a temporary walks a destroyed object and yields nothing.
    juce::StringArray items;

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        items.add (it.getItem().text);

    REQUIRE (items.size() == 1);
    REQUIRE (items[0] == "Colour");
}

TEST_CASE ("a lane paints its own colour, and its position when it has none",
           "[ui][playlist][colour]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    PlaylistComponent playlist { document, engine, editorState };
    playlist.setSize (1200, 500);
    playlist.setVisible (true);
    playlist.refresh();
    playlist.resized();

    // The band down a header's left edge is the lane's identity, and it used to
    // be a pure function of the row's INDEX - so moving a lane repainted it and
    // nothing could choose.
    auto track = firstTrack (document);
    REQUIRE (track.isValid());
    REQUIRE_FALSE (entityColour::stored (track).has_value());

    const auto chosen = juce::Colour::fromString (entityColour::defaultHex (6));

    // Not the colour the position would have given, or this would pass whatever
    // the painter read.
    REQUIRE (chosen != tokens::colour::channelColour (0));
    REQUIRE (juce::exactlyEqual (testing::coverageOf (testing::render (playlist), chosen),
                                 0.0f));

    ProjectEdits::setColour (track, entityColour::defaultHex (6), nullptr);
    playlist.refresh();

    // Painted, not merely stored: the band and the clips on the lane both read
    // it, so the arrangement says what the header says.
    CHECK (testing::coverageOf (testing::render (playlist), chosen) > 0.0f);
}
