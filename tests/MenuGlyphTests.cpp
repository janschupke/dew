// Getting a glyph into a menu row, and out the other side onto the screen.
//
// The mechanism is JUCE's own: a PopupMenu::Item carries a Drawable and
// DewLookAndFeel used to throw it away. What is asserted here is the part of
// that which could silently stop working - that the picture reaches the paint,
// that it is painted in the row's own colour and inside its own column, that
// the item's TEXT was left alone, and that going round ComboBox::addItem to
// reach the root menu did not break the bookkeeping that walks it.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "model/Ids.h"
#include "ui/ChannelRackComponent.h"
#include "ui/ChannelRackHeader.h"
#include "ui/EditorState.h"
#include "ui/MenuSeam.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Glyphs.h"
#include "ui/design/Icons.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/Theme.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

#include "FixtureProject.h"
#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;
using namespace dew::tokens;

namespace
{

/** Where drawPopupMenuItem puts a row's glyph.

    Derived the same way the drawing derives it rather than written out, so a
    change to the insets moves the test's window with the code's.
*/
juce::Rectangle<int> glyphColumnOf (juce::Rectangle<int> area)
{
    auto content = area.reduced (space::xs, space::xxs).reduced (space::md, 0);
    content.removeFromLeft (size::glyphColumn); // the tick gutter, always reserved
    content.removeFromLeft (space::xs);
    return content.removeFromLeft (size::glyphColumn);
}

/** How many pixels the two images disagree about, inside `region`. */
int differencesIn (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> region)
{
    auto differing = 0;

    for (auto y = region.getY(); y < region.getBottom(); ++y)
        for (auto x = region.getX(); x < region.getRight(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                ++differing;

    return differing;
}

constexpr int rowWidth = 220;

/** The first descendant of a type, at any depth.

    Component::findChildWithID is NOT recursive, and neither is a walk over
    getChildren(): the rack's rows are inside a viewport inside a holder, so
    anything shallower than this finds nothing.
*/
template <typename Type> Type* findDescendant (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (auto* match = dynamic_cast<Type*> (child))
            return match;

        if (auto* found = findDescendant<Type> (*child))
            return found;
    }

    return nullptr;
}

/** One menu row, painted. `glyph` null draws the same row without a picture.

    The label is deliberately EMPTY: with no text, the only thing that can
    differ between a glyphed row and a plain one is the glyph, which is what
    makes comparing the two a measurement rather than a guess.
*/
juce::Image paintRow (DewLookAndFeel& laf, const MenuGlyph* glyph, bool isHighlighted,
                      const juce::String& text = {})
{
    const auto area = juce::Rectangle<int> (0, 0, rowWidth, size::controlHeight);
    juce::Image image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);

    juce::Graphics g (image);
    g.fillAll (colour::surface);
    laf.drawPopupMenuItem (g, area, false, true, isHighlighted, false, false, text, {}, glyph,
                           nullptr);

    return image;
}

} // namespace

TEST_CASE ("a glyphed item carries its picture and keeps its text", "[design][menuglyph]")
{
    juce::PopupMenu menu;
    addGlyphItem (menu, 7, "Rename", glyph::forAction (glyph::Action::rename));

    // A NAMED menu: MenuItemIterator keeps a reference to what it was given, so
    // walking a temporary walks a destroyed object and reports nothing.
    juce::PopupMenu::MenuItemIterator it (menu);
    REQUIRE (it.next());

    const auto& item = it.getItem();

    CHECK (item.itemID == 7);
    CHECK (item.image != nullptr);
    CHECK (dynamic_cast<const MenuGlyph*> (item.image.get()) != nullptr);

    // The point of using the image slot rather than smuggling a sentinel into
    // the text: dew::menuItems is the seam every menu test in the repository
    // reads, and it reads exactly this string.
    CHECK (item.text == "Rename");
    CHECK (menuItems (menu)[0] == "Rename");
}

TEST_CASE ("a glyphed submenu keeps its children", "[design][menuglyph]")
{
    juce::PopupMenu kinds;
    addGlyphItem (kinds, 1, "Synth", glyph::forInstrument (InstrumentType::synth));
    addGlyphItem (kinds, 2, "Audio", glyph::forInstrument (InstrumentType::audio));

    juce::PopupMenu menu;
    addGlyphSubMenu (menu, "Add channel", std::move (kinds), glyph::forAction (glyph::Action::add));

    juce::PopupMenu::MenuItemIterator it (menu);
    REQUIRE (it.next());

    const auto& item = it.getItem();

    CHECK (item.text == "Add channel");
    CHECK (dynamic_cast<const MenuGlyph*> (item.image.get()) != nullptr);
    REQUIRE (item.subMenu != nullptr);
    CHECK (item.subMenu->getNumItems() == 2);
}

TEST_CASE ("a dropdown's bookkeeping survives being given a glyph", "[design][menuglyph]")
{
    // addGlyphItem goes round ComboBox::addItem to reach the root menu, because
    // addItem takes a text and an id and offers nowhere to put a picture.
    // Everything ComboBox does by walking that same menu has to still work.
    DewDropdown box;

    addGlyphItem (box, 11, "Sine", glyph::forWaveform (Waveform::sine));
    addGlyphItem (box, 22, "Saw", glyph::forWaveform (Waveform::saw));
    box.addItem ("Plain", 33);

    CHECK (box.getNumItems() == 3);
    CHECK (box.getItemText (0) == "Sine");
    CHECK (box.getItemId (1) == 22);
    CHECK (box.indexOfItemId (22) == 1);

    box.setSelectedId (22, juce::dontSendNotification);
    CHECK (box.getText() == "Saw");
    CHECK (! selectedGlyph (box).isEmpty());
    CHECK (hasGlyphs (box));

    // An option with no picture reports none, rather than the last one seen.
    box.setSelectedId (33, juce::dontSendNotification);
    CHECK (selectedGlyph (box).isEmpty());

    // The glyphs live IN the menu, so a clear takes them with it. A list kept
    // beside the menu would have survived this, and the closed box would have
    // gone on drawing an option that no longer exists.
    box.clear (juce::dontSendNotification);
    CHECK (box.getNumItems() == 0);
    CHECK (! hasGlyphs (box));
    CHECK (selectedGlyph (box).isEmpty());
}

TEST_CASE ("a row's glyph draws in its own column and nowhere else", "[design][menuglyph]")
{
    DewLookAndFeel laf;
    const MenuGlyph glyph { icons::plus() };

    const auto restore = theme::current();

    for (const auto kind : { theme::Kind::dark, theme::Kind::highContrast })
    {
        theme::applyPalette (kind);
        laf.applyPalette();

        for (const auto highlighted : { false, true })
        {
            const auto with = paintRow (laf, &glyph, highlighted);
            const auto without = paintRow (laf, nullptr, highlighted);

            const auto area = juce::Rectangle<int> (0, 0, rowWidth, size::controlHeight);
            const auto column = glyphColumnOf (area);

            INFO ("theme " << (int) kind << ", highlighted " << highlighted);

            // It drew.
            CHECK (differencesIn (with, without, column) > 20);

            // And it drew only there: a glyph that leaked past its column would
            // sit under the label of every row that has one.
            CHECK (differencesIn (with, without, area) == differencesIn (with, without, column));
        }
    }

    theme::applyPalette (restore);
}

TEST_CASE ("a row's glyph takes the row's own text colour", "[design][menuglyph]")
{
    // The reason this matters is what it saves: painted in the colour the label
    // is already painted in, a glyph inherits the contrast ContrastTests has
    // proven for menu text in both palettes and both states, and adds no new
    // pair to prove.
    DewLookAndFeel laf;
    const MenuGlyph glyph { icons::plus() };

    const auto restore = theme::current();

    for (const auto kind : { theme::Kind::dark, theme::Kind::highContrast })
    {
        theme::applyPalette (kind);
        laf.applyPalette();

        const auto plain = paintRow (laf, &glyph, false);
        const auto highlighted = paintRow (laf, &glyph, true);

        INFO ("theme " << (int) kind);
        CHECK (coverageOf (plain, colour::textPrimary) > 0.0f);
        CHECK (coverageOf (highlighted, colour::textOnAccent) > 0.0f);
    }

    theme::applyPalette (restore);
}

TEST_CASE ("a real context menu is glyphed all the way through", "[design][menuglyph]")
{
    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    document.getState().copyPropertiesAndChildrenFrom (fixtureProject(), nullptr);

    ChannelRackComponent rack { document, engine, editorState };
    rack.setSize (900, 600);

    auto* header = findDescendant<ChannelRackHeader> (rack);
    REQUIRE (header != nullptr);

    const auto menu = header->buildMenu();
    auto rows = 0;

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();

        if (item.isSeparator)
            continue;

        ++rows;
        INFO ("row: " << item.text);

        // Every row, not most of them. A menu where some rows are indented and
        // others are not reads as misaligned, which is the same reason the tick
        // gutter is always reserved.
        CHECK (dynamic_cast<const MenuGlyph*> (item.image.get()) != nullptr);
    }

    // MenuItemIterator does not recurse, so this counts the rows of the TOP
    // menu: rename, colour, add channel, remove channel.
    CHECK (rows == 4);
}

TEST_CASE ("the add-channel submenu glyphs the kind it makes", "[design][menuglyph]")
{
    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    document.getState().copyPropertiesAndChildrenFrom (fixtureProject(), nullptr);

    ChannelRackComponent rack { document, engine, editorState };
    rack.setSize (900, 600);

    const auto channelCount = [&document]
    {
        auto n = 0;

        for (const auto& channel : document.getState())
            if (channel.hasType (ids::CHANNEL))
                ++n;

        return n;
    };

    const auto sourceOfLast = [&document]
    {
        juce::String source;

        for (const auto& channel : document.getState())
            if (channel.hasType (ids::CHANNEL))
                source = channel[ids::source].toString();

        return source;
    };

    auto expected = channelCount();

    // The three rows are what instrumentDescriptors() offers, which is the list
    // the submenu is built from - so this walks the same registry the menu does
    // rather than a second copy of it.
    const std::pair<ChannelRackHeader::MenuItem, InstrumentType> rows[] {
        { ChannelRackHeader::MenuItem::addSynth, InstrumentType::synth },
        { ChannelRackHeader::MenuItem::addAudio, InstrumentType::audio },
        { ChannelRackHeader::MenuItem::addSoundFont, InstrumentType::soundfont },
    };

    for (const auto& [item, type] : rows)
    {
        const auto first = document.getState().getChildWithName (ids::CHANNEL);
        REQUIRE (rack.applyChannelMenuChoice ((int) first[ids::id], (int) item));

        ++expected;
        INFO ("kind: " << (int) type);
        CHECK (channelCount() == expected);
        CHECK (sourceOfLast() == instrumentDescriptor (type).id);
    }
}
