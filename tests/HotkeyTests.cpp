#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Settings.h"
#include "ui/Hotkeys.h"
#include "ui/MainComponent.h"

using namespace dew;
using hotkeys::ViewCommand;

namespace
{

/** The command ids are a contiguous enum, so every one of them can be walked
    without writing a second list beside the first - which is the whole point of
    a registry.
*/
constexpr int firstCommand = CommandIDs::fileNew;
constexpr int lastCommand = CommandIDs::viewUiScale175;

bool sameStroke (const hotkeys::Stroke& a, const hotkeys::Stroke& b) noexcept
{
    // A row with no key code is a menu item and nothing more - the UI scales are
    // four of them. Two of those "share" a stroke only in the sense that neither
    // has one, so comparing them is not a collision.
    if (a.keyCode == 0 || b.keyCode == 0)
        return false;

    return a.keyCode == b.keyCode && a.modifiers == b.modifiers;
}

} // namespace

TEST_CASE ("every command dew declares has a binding, and every binding a command", "[ui][hotkeys]")
{
    // The drift gate. The application used to enumerate its commands in one
    // place, name them in a second and bind them in a third, and nothing
    // checked the three against each other.
    for (int id = firstCommand; id <= lastCommand; ++id)
    {
        INFO ("command id 0x" << juce::String::toHexString (id));
        REQUIRE (hotkeys::find (id) != nullptr);
    }

    for (const auto& binding : hotkeys::application())
    {
        INFO (binding.name);
        CHECK (binding.action >= firstCommand);
        CHECK (binding.action <= lastCommand);
        CHECK (juce::String (binding.name).isNotEmpty());
        CHECK (juce::String (binding.description).isNotEmpty());
        CHECK (juce::String (binding.category).isNotEmpty());
    }

    REQUIRE ((int) hotkeys::application().size() == lastCommand - firstCommand + 1);
}

TEST_CASE ("no two commands answer to the same key", "[ui][hotkeys]")
{
    const auto& table = hotkeys::application();

    for (size_t i = 0; i < table.size(); ++i)
        for (size_t j = i + 1; j < table.size(); ++j)
        {
            INFO (table[i].name << " and " << table[j].name << " share a key");
            CHECK_FALSE (sameStroke (table[i].stroke, table[j].stroke));
        }

    // The timeline map may spell one command two ways - `+` and `=` are the
    // same key on most layouts, and select-all answers to command and control -
    // but two DIFFERENT commands on one stroke is a collision.
    const auto& views = hotkeys::timeline();

    for (size_t i = 0; i < views.size(); ++i)
        for (size_t j = i + 1; j < views.size(); ++j)
            if (sameStroke (views[i].stroke, views[j].stroke))
            {
                INFO (views[i].name << " and " << views[j].name << " share a key");
                CHECK (views[i].action == views[j].action);
            }
}

TEST_CASE ("a modified digit is not a timeline command", "[ui][hotkeys]")
{
    // THE regression test. The map this replaced compared a character and
    // nothing else, so cmd-1 resolved to the select tool in whichever timeline
    // view had focus and was consumed there - which meant no mod-plus-number
    // binding could ever have reached the window's key mappings.
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('1')) == ViewCommand::selectTool);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('2')) == ViewCommand::paintTool);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('3')) == ViewCommand::eraseTool);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('0')) == ViewCommand::zoomToFit);

    for (const auto digit : { '0', '1', '2', '3', '4', '5' })
    {
        INFO ("cmd-" << juce::String::charToString ((juce::juce_wchar) digit));
        CHECK (
            hotkeys::viewCommandFor (juce::KeyPress (digit, juce::ModifierKeys::commandModifier, 0))
            == ViewCommand::none);
    }

    // And the application binds those, which is what the digits were being
    // taken away from.
    REQUIRE (hotkeys::find (CommandIDs::viewChannelRack) != nullptr);
    CHECK (hotkeys::find (CommandIDs::viewChannelRack)->stroke.keyCode == '1');
    CHECK (hotkeys::find (CommandIDs::viewChannelRack)->stroke.modifiers
           == juce::ModifierKeys::commandModifier);
}

TEST_CASE ("a key means the same thing in every timeline view", "[ui][hotkeys]")
{
    // The playlist bound four keys, the piano roll bound six, and the sequencer
    // and mixer did not override keyPressed at all - so the same key did three
    // different things depending on which tab was in front.
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('=')) == ViewCommand::zoomIn);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('+')) == ViewCommand::zoomIn);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('-')) == ViewCommand::zoomOut);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('_')) == ViewCommand::zoomOut);

    CHECK (hotkeys::viewCommandFor (juce::KeyPress (juce::KeyPress::escapeKey))
           == ViewCommand::clearSelection);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress (juce::KeyPress::deleteKey))
           == ViewCommand::deleteSelection);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress (juce::KeyPress::backspaceKey))
           == ViewCommand::deleteSelection);

    // Select-all takes command OR ctrl. It was command-only in the piano roll,
    // so it did nothing on a machine driven with ctrl even though rubber-band
    // select on the same modifier worked.
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
           == ViewCommand::selectAll);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0))
           == ViewCommand::selectAll);

    // A bare letter is not a command: the editors bind q and shift-r
    // themselves, and a map that swallowed them would take them away.
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('a')) == ViewCommand::none);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('q')) == ViewCommand::none);
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('r')) == ViewCommand::none);
}

TEST_CASE ("a binding is reachable by code and by character", "[ui][hotkeys]")
{
    // A KeyPress built from a code alone carries no text character - which is
    // every KeyPress a test makes - and a real press of a printable key carries
    // both. A map reachable only one way is either untestable or broken in use.
    for (const auto& binding : hotkeys::timeline())
    {
        INFO (binding.name);

        const auto fromCode = hotkeys::keyPressFor (binding.stroke);
        CHECK (hotkeys::matches (binding.stroke, fromCode));
        CHECK (hotkeys::viewCommandFor (fromCode) == binding.action);

        if (juce::CharacterFunctions::isPrintable ((juce::juce_wchar) binding.stroke.keyCode))
        {
            const juce::KeyPress typed { binding.stroke.keyCode,
                                         juce::ModifierKeys (binding.stroke.modifiers),
                                         (juce::juce_wchar) binding.stroke.keyCode };

            CHECK (hotkeys::viewCommandFor (typed) == binding.action);
        }
    }
}

TEST_CASE ("record stays bare and the score compiles on a modifier", "[ui][hotkeys]")
{
    // Bare `r` was bound twice: Record here, and the piano roll's randomize
    // dialog in its own keyPressed. Whichever had focus won, so recording
    // stopped working the moment you looked at the roll. Randomize moved to
    // shift-r; this is the half that has to stay where it is.
    const auto* record = hotkeys::find (CommandIDs::transportRecord);
    REQUIRE (record != nullptr);
    CHECK (record->stroke.keyCode == 'r');
    CHECK (record->stroke.modifiers == 0);

    // And shift-r is not a timeline command either, so the roll can have it.
    CHECK (hotkeys::viewCommandFor (juce::KeyPress ('r', juce::ModifierKeys::shiftModifier, 'R'))
           == ViewCommand::none);
}

TEST_CASE ("the editors are reachable from the keyboard", "[ui][hotkeys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component { false };
    component.setSize (1280, 800);

    REQUIRE (component.getNumEditorTabs() == Settings::numTabs);

    // One command per tab, and nothing points past the end.
    const int perTab[] { CommandIDs::viewChannelRack, CommandIDs::viewPianoRoll,
                         CommandIDs::viewPlaylist, CommandIDs::viewMixer, CommandIDs::viewScore };

    REQUIRE ((int) std::size (perTab) == Settings::numTabs);

    for (int i = 0; i < Settings::numTabs; ++i)
    {
        INFO ("tab " << i);
        REQUIRE (hotkeys::find (perTab[i]) != nullptr);

        component.showTab (i);
        CHECK (component.getActiveTab() == i);
    }

    // Out of range clamps rather than leaving the editor on nothing.
    component.showTab (99);
    CHECK (component.getActiveTab() == Settings::numTabs - 1);

    component.showTab (-4);
    CHECK (component.getActiveTab() == 0);
}

TEST_CASE ("the next and previous editor wrap", "[ui][hotkeys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component { false };
    component.setSize (1280, 800);

    const auto last = Settings::numTabs - 1;

    component.showTab (0);
    component.showAdjacentTab (-1);
    CHECK (component.getActiveTab() == last);

    component.showAdjacentTab (1);
    CHECK (component.getActiveTab() == 0);

    for (int i = 0; i < Settings::numTabs; ++i)
        component.showAdjacentTab (1);

    // All the way round and back to where it started.
    CHECK (component.getActiveTab() == 0);
}

TEST_CASE ("the instrument panel folds from the keyboard", "[ui][hotkeys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MainComponent component { false };
    component.setSize (1280, 800);

    const auto open = component.getInstrumentPanelWidthForTesting();
    REQUIRE (open > 0);

    // Motion is off outside a running application, so the fold lands rather
    // than being caught halfway.
    component.toggleInstrumentPanel();
    CHECK (component.getInstrumentPanelWidthForTesting() < open);

    component.toggleInstrumentPanel();
    CHECK (component.getInstrumentPanelWidthForTesting() == open);
}
