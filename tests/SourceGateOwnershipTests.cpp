#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "SourceScan.h"
#include "model/AutomationTargets.h"
#include "model/ModuleCatalog.h"

using namespace dew::testing;
using namespace dew;

// =============================================================================
// The single-owner gates.
//
// Each of these says the same thing about a different thing: there is one place
// that owns it, and everywhere else goes through that place. DewControls owns
// the controls, Ids owns the property names, Gestures owns the wheel and the
// drag scale, ProjectEdits owns an undoable write, Hotkeys owns a key.
//
// So each carries an exemption naming the owner, and that exemption is the only
// kind that is ever right: it names a definition site rather than a file that
// happens to be awkward.
// =============================================================================

/** True when `line` hands a non-ASCII literal to juce::String's const char*
    constructor, which is the one place JUCE decodes those bytes as ASCII.

    Hoisted out of the gate so the gate can be pointed at a line it should catch
    and a line it should not, which is the only way to know a scanner works.
*/
static bool feedsAsciiConstructor (const juce::String& line)
{
    for (int i = 0; i < line.length();)
    {
        if (line[i] != '"')
        {
            ++i;
            continue;
        }

        const auto open = i;
        auto close = -1;

        for (int j = i + 1; j < line.length(); ++j)
        {
            if (line[j] == '\\')
            {
                ++j;
                continue;
            }

            if (line[j] == '"')
            {
                close = j;
                break;
            }
        }

        if (close < 0)
            return false;

        auto holdsNonAscii = false;

        for (int j = open + 1; j < close; ++j)
            if (line[j] > 127)
                holdsNonAscii = true;

        if (holdsNonAscii)
        {
            const auto before = line.substring (0, open).trimEnd();
            const auto after = line.substring (close + 1).trim();

            // Opening a concatenation means the literal is the LEFT operand, so
            // operator+ (const char*, const String&) builds a String from it
            // first. A literal already on the right of a + goes through
            // operator+=, which reads UTF-8 and is correct.
            if (after.startsWith ("+") && ! before.endsWith ("+") && ! before.endsWith ("<<"))
                return true;

            // The same constructor, reached directly. String (CharPointer_UTF8
            // ("...")) is the escape hatch and does not match: it does not end
            // in "String (".
            if (before.endsWith ("juce::String (") || before.endsWith ("juce::String(")
                || before.endsWith ("String (") || before.endsWith ("String("))
                return true;
        }

        i = close + 1;
    }

    return false;
}

TEST_CASE ("no source declares a bare juce::ComboBox", "[build][gate]")
{
    // Same rule as the checkbox above, and the same reason: DewLookAndFeel
    // paints the box, the arrow and the menu, so what a stock ComboBox lacks is
    // not an appearance but a cursor - and JUCE does not inherit one from a
    // parent, so a dropdown left alone shows an arrow beside a button showing a
    // hand. Sixteen boxes in seven panels is past the count at which a habit
    // stays reliable.
    //
    // Declarations only. A function taking a juce::ComboBox& is taking the base
    // class of a DewDropdown, which is correct.
    //
    // No exemption. DewControls.h used to have one, and it was held open by a
    // doc comment - the wrapped line "juce::ComboBox rather than painted over
    // it" - because the reader counted prose as code. DewDropdown's own
    // declaration reads "class DewDropdown : public juce::ComboBox", which this
    // does not match and never did.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            return trimmed.startsWith ("juce::ComboBox ") || trimmed.contains ("juce::ComboBox>()")
                   || trimmed.contains ("new juce::ComboBox");
        });

    INFO ("stock combo boxes:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source declares a bare juce::ToggleButton", "[build][gate]")
{
    // juce::Button completes a click for whichever mouse button pressed it, so
    // a stock ToggleButton flips on a right-click - which is a gesture that in
    // every other part of dew means "show me a menu" and never means "do it".
    // DewCheckbox is that control with the press filtered, and the five that
    // were stock lived in two dialogs where nobody thought to check.
    //
    // Declarations only: the LookAndFeel names ToggleButton's colour ids, and
    // theming the stock control is the reason DewCheckbox does not repaint it.
    //
    // No exemption, for the same reason as the combo box above: "class
    // DewCheckbox : public juce::ToggleButton" does not start with the type, so
    // the definition site never needed one.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            return trimmed.startsWith ("juce::ToggleButton ")
                   || trimmed.contains ("juce::ToggleButton>");
        });

    INFO ("stock toggle buttons:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source starts a concatenation with a non-ASCII literal", "[build][gate]")
{
    // juce::String decodes an 8-bit literal two different ways depending on
    // which side of the + it is on, and nothing warns:
    //
    //     String::String (const char*)      -> CharPointer_ASCII, mangles UTF-8
    //     String::operator+= (const char*)  -> CharPointer_UTF8,  correct
    //
    // So `name + " — "` is right and `" — " + name` is not, and the window title
    // was the second one: "dew — Untitled" reached the title bar with the em
    // dash split into three characters. RenderPanel.cpp had already written the
    // rule down in a comment, one file away, and the comment did not stop it.
    //
    // Invisible in review - the same file can hold both forms and only one is
    // wrong - which is exactly the kind of rule that has to be a scanner.
    const auto found = offenders ([] (const juce::String& line)
                                  { return feedsAsciiConstructor (line); });

    INFO ("non-ASCII literals decoded as ASCII:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());

    // Control case: a scanner that cannot see the defect it was written for is
    // not a gate. Both lines below are the window title, before and after.
    const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x94"));

    CHECK (feedsAsciiConstructor ("setName (\"dew " + dash + " \" + name);"));
    CHECK_FALSE (feedsAsciiConstructor ("title += \" " + dash + " \";"));
    CHECK_FALSE (feedsAsciiConstructor ("summary = seconds + \"  " + dash + "  \" + rate;"));
}

TEST_CASE ("no source spells an automatable parameter as a string literal", "[build][gate]")
{
    // The twenty names that were the actual defect: the snapshot builder held a
    // table mapping "cutoff", "roomSize", "midFreq" and the rest to an enum, and
    // it was the only place in production that wrote a property name by hand.
    //
    // The failure was silent in the worst way. Renaming an identifier in Ids.h
    // compiled cleanly, the schema and the editor followed the new name, and
    // every automation curve pointing at the old one simply stopped doing
    // anything - because a name that matches nothing is indistinguishable from
    // an automation of nothing.
    //
    // Scoped to automatable parameters rather than every identifier, and taken
    // from the catalog rather than scraped, because dew's identifiers also
    // include node types (CHANNEL, MIXER) that are legitimate UI captions, and
    // property names that are also legitimate VALUES - "loop", "record",
    // "wavetable", "drive". Widening this beyond the parameters would be a gate
    // that cries wolf, which is a gate people turn off.
    // ONE call rather than five table lookups written out here. The gate used to
    // walk each per-scope table by hand, so a table added beside them was a
    // table the gate silently did not cover.
    auto names = dew::automatableParameterNames();

    // Two whole directories are exempt for the same reason the effect ids below
    // are. ui/design/icons is a registry of ICON names, and "mute" is one of
    // them; src/lang is the score language, whose keywords are its own
    // vocabulary and address its own tree, not the project's. Both became
    // offenders the moment mute turned into an automatable parameter, and
    // neither is the defect this gate exists to catch - which is a property name
    // written by hand where a property is being RESOLVED.
    //
    // Ids.h was a third, and it was never needed: DEW_DECLARE_ID spells a name
    // with the preprocessor's # operator, so "cutoff" does not appear as a
    // literal in the file that declares it and this gate could never have seen
    // one there.
    //
    // An effect's id and one of its parameters share a spelling in one case -
    // "drive" is both - and the id is a value a file legitimately contains.
    for (const auto& descriptor : dew::effectDescriptors())
        names.removeString (descriptor.id);

    // Control case: a gate over an empty list is not a gate.
    REQUIRE (names.size() > 15);
    REQUIRE (names.contains ("cutoff"));
    REQUIRE (names.contains ("midFreq"));

    const auto found = dew::testing::offenders (
        [&names] (const juce::String& line)
        {
            // The exemption is the DIRECTORY ui/design/icons, not a file in it.
            // The catalog names its own icons as strings - { "mute", mute } - and
            // several of those names are also parameter names. It was one file and
            // one entry; splitting it into three would have meant three entries,
            // which is the list SourceScan.h warns a fourth file falls off.
            for (const auto& name : names)
                if (line.contains ("\"" + name + "\""))
                    return true;

            return false;
        },
        { "ui/design/icons", "lang" });

    INFO ("automatable parameters written as string literals:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no view reads the wheel or the drag scale for itself", "[build][gate][gesture]")
{
    // Three wheel speeds - 4, 6 and 8 steps a notch - and one view of three
    // reading isReversed, which JUCE reports rather than applies. The two that
    // ignored it scrolled backwards for anyone running the Mac default, and
    // nobody noticed because each view was right about itself.
    //
    // Gestures.h is where the reading happens, so it is the one place allowed
    // to touch the raw fields.
    //
    // Handing JUCE the drag scale is allowed when the line NAMES gesture::,
    // because a call that quotes the scale is the opposite of deciding one.
    // That is what the exemption used to stand in for, and standing in for it
    // was the problem: the list held the two files that happened to make the
    // call, DewKnob.cpp joined it when the knob left DewControls.cpp, and the
    // gate went red for exactly that - a moved file falling off a list of
    // names. Asking about the call instead needs no list, and the knob's own
    // fine-drag arithmetic moved to gesture::dragPixelsFor where it belonged.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.contains ("setMouseDragSensitivity"))
                return ! trimmed.contains ("gesture::");

            return line.contains ("wheel.deltaX") || line.contains ("wheel.deltaY")
                   || line.contains ("wheel.isReversed");
        },
        { "ui/design/Gestures.h" });

    INFO ("views reading the wheel or the drag scale directly:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no editor writes an undoable property by hand", "[build][gate][undo]")
{
    // ProjectEdits had no scalar setter at all, so all twenty-seven property
    // writes in src/ui went straight to ValueTree and each re-implemented the
    // transaction rule around it - copy-pasted five times, and MISSING from a
    // sixth. The consequence was invisible in the file that had the bug and
    // obvious only across all six: dragging an audio channel's fade made one
    // undo step per frame.
    //
    // A write with no UndoManager is not caught, and deliberately: writing to a
    // detached copy nobody can undo is a different thing, and it says so where
    // it happens.
    //
    // This asked for the SPELLING "&undo" or "getUndoManager()", and every
    // undoable write in the tree passes a pointer parameter named undo - so the
    // gate matched nothing, anywhere, for as long as it has existed. It was
    // green because it could not see. Its three exemptions read as unnecessary
    // for the same reason, and two of them - ProjectFactory.cpp and
    // ProjectSchema.cpp - were a standing licence to write an undoable property
    // by hand in exactly the two files that build the document.
    //
    // So the question is the SHAPE: a setProperty given an UndoManager, which
    // is any third argument that is not nullptr. Only a whole statement is
    // judged, because a wrapped call puts its nullptr on the next line and
    // ProjectDocument has one of those.
    //
    // Both exemptions name an owner rather than a file that is awkward.
    // model/edits used to be ProjectEdits.cpp, and when that file became seven
    // the list would have had to name all seven - the shape SourceScan.h warns
    // about: a list of five that the sixth silently escapes. ScoreBake is the
    // second owner and says so in its own header - "everything happens inside
    // one UndoManager transaction, so a bake is one undo step" - which is a
    // claim ProjectEdits::setProperty cannot carry, because it names a
    // transaction per call.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (! trimmed.endsWith (");") || ! trimmed.contains (".setProperty ("))
                return false;

            if (trimmed.contains ("ProjectEdits::setProperty"))
                return false;

            return ! trimmed.contains ("nullptr");
        },
        { "model/edits", "model/ScoreBake.cpp" });

    INFO ("undoable property writes outside ProjectEdits:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no source binds a key outside the hotkey registry", "[build][gate][hotkeys]")
{
    // There used to be two key tables that could not see each other: fifteen
    // addDefaultKeypress calls written inline in DewApplication::getCommandInfo,
    // and an if-chain in Gestures.cpp for the timeline views. Neither was wrong
    // about itself, and between them cmd-1 was swallowed by whichever view had
    // focus and bare `r` meant two different things.
    //
    // Hotkeys.cpp is where a binding is spelled, so it is exempt.
    // ScoreEditorComponent.cpp is the other one: while its completion popup is
    // open it owns Up, Down, Return, Tab and Escape, and that is a modal
    // handler rather than a binding - nothing outside that popup can reach
    // those keys, so putting them in a table shared with the menu bar would say
    // something untrue.
    //
    // Hotkeys.h was a third and never spelled a key at all; it declares the
    // vocabulary the .cpp binds. The day a table in the header names one, the
    // commit that puts it there puts the exemption back.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            return line.contains ("addDefaultKeypress (") || line.contains ("juce::KeyPress (")
                   || line.contains ("KeyPress::createFromDescription");
        },
        { "ui/Hotkeys.cpp", "ui/ScoreEditorComponent.cpp" });

    INFO ("keys bound outside the registry:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no menu-bar menu carries a popup's second line", "[build][gate][menus]")
{
    // DewLookAndFeel::menuRow packs a sentence under a label by joining the two
    // with a newline, and DewLookAndFeel::drawPopupMenuItem is what knows to
    // split it again. That is a POPUP affordance, and the menu bar is not a
    // popup: on macOS it is the native one - DewApplication calls
    // MenuBarModel::setMacMainMenu - so its items are NSMenuItems that no look
    // and feel of dew's ever paints. The Demos menu shipped eight rows that way
    // and what a person read was the description.
    //
    // Asked as a shape rather than as a path, so the rule survives the file
    // being split: a translation unit that implements getMenuForIndex is
    // building the menu bar, and must not call menuRow anywhere in it. The two
    // legitimate callers - the instrument panel's presets and the effect
    // chain's - implement no such thing and are not touched.
    juce::StringArray found;
    auto menuBarFiles = 0;

    for (const auto& file : sourceFiles())
    {
        const auto lines = codeLinesWithNumbersOf (file);

        const auto buildsTheMenuBar = std::any_of (
            lines.begin(), lines.end(),
            [] (const CodeLine& line) { return line.text.contains ("getMenuForIndex"); });

        if (! buildsTheMenuBar)
            continue;

        ++menuBarFiles;

        for (const auto& line : lines)
            if (line.text.contains ("menuRow"))
                found.add (relativePathOf (file) + ":" + juce::String (line.number) + "  "
                           + line.text.trim());
    }

    // Control case: a gate that found no menu bar at all would pass in silence,
    // which is exactly how this class of scanner dies.
    INFO ("translation units building the menu bar: " << menuBarFiles);
    REQUIRE (menuBarFiles > 0);

    INFO ("menu-bar rows carrying a second line:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("a control that arms a popup press also disarms it", "[build][gate][gesture]")
{
    // The half of the rule that a walk of the window cannot check, and that
    // being unable to check it is exactly how it went missing for months.
    //
    // Every control refused a right press in mouseDown and stopped there.
    // juce::Button re-arms itself in mouseDrag - updateState (over, true), for
    // whichever mouse button is held - and completes the click in mouseUp, so a
    // right-press that moved one pixel fired the button anyway. The piano
    // roll's octave pair is where it was noticed; it was true of every button
    // in the application.
    //
    // Not testable through the window: Button::isMouseSourceOver asks
    // Component::isMouseOver, which reads the real pointer, and a headless
    // harness has none - so a synthetic right-drag never reaches the state the
    // defect needs. What IS checkable is that the three phases were written
    // together, which is what PopupPress exists to make one decision.
    juce::StringArray found;
    auto arming = 0;

    for (const auto& file : sourceFiles())
    {
        const auto lines = codeLinesWithNumbersOf (file);

        const auto has = [&lines] (const char* call)
        {
            return std::any_of (lines.begin(), lines.end(), [call] (const CodeLine& line)
                                { return line.text.contains (call); });
        };

        if (! has ("popupPress.down ("))
            continue;

        ++arming;

        if (! has ("popupPress.dragging()") || ! has ("popupPress.releasing()"))
            found.add (relativePathOf (file)
                       + "  arms a popup press and does not refuse the drag or the release");
    }

    // Control case: a gate that found nothing arming one would pass in silence.
    INFO ("translation units arming a popup press: " << arming);
    REQUIRE (arming > 3);

    INFO ("controls that refuse only the press:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}
