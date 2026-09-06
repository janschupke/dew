#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "SourceScan.h"
#include "control/ControlOps.h"
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
    // DewCheckbox : public PopupSafeButton<juce::ToggleButton>" does not start
    // with the type, so the definition site never needed one. It DOES name the
    // type inside angle brackets, which is the one shape this predicate has to
    // let through - a guarded toggle is the sanctioned control, not a stock one.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            if (trimmed.contains ("PopupSafeButton<"))
                return false;

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

    // src/lang is exempt for the same reason the effect ids below are: the score
    // language's keywords are its own vocabulary and address its own tree, not
    // the project's, and a keyword that happens to share a spelling with a
    // parameter is not the defect this gate exists to catch - which is a
    // property name written by hand where a property is being RESOLVED.
    //
    // ui/design/icons was exempt too, because its registry named an icon "mute"
    // and mute is an automatable parameter. That icon is gone - one on/off
    // glyph now, not a crossed speaker beside a power symbol - and the gate
    // reported the exemption as suppressing nothing, which is the mechanism
    // that keeps this list from becoming a place things are added to.
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

    // And the same for a note transform's verb: "transpose" is one of those and
    // is also a soundfont channel's pitch offset. Taken from the vocabulary
    // rather than typed here, so a verb added beside it is stripped too.
    for (const auto& verb : dew::control::noteTransformVerbs())
        names.removeString (verb);

    // Control case: a gate over an empty list is not a gate.
    REQUIRE (names.size() > 15);
    REQUIRE (names.contains ("cutoff"));
    REQUIRE (names.contains ("midFreq"));

    const auto found = dew::testing::offenders (
        [&names] (const juce::String& line)
        {
            for (const auto& name : names)
                if (line.contains ("\"" + name + "\""))
                    return true;

            return false;
        },
        { "lang" });

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

TEST_CASE ("no editor writes the rotary gesture by hand", "[build][gate][undo]")
{
    // Nine panels wrote out the same three callbacks and kept the same two
    // flags beside them - the oscillator section, the two sampled sections, the
    // rack row, the playlist track header, the mixer strip, the effect card,
    // the FM matrix and the instrument panel. Two of the copies were
    // byte-identical. It is the mechanism that makes dragging a knob ONE undo
    // step rather than four hundred, and the copies had drifted in exactly the
    // way a copy does: three of them left a refused write advancing the
    // gesture, and two shared their flag with a control that has a DIFFERENT
    // protocol - a waveform trim, and a number field with no edit-end - so
    // typing a value and then nudging a knob landed in one undo step.
    //
    // The question is the SHAPE that only a rotary has: a control with both a
    // start and an END. A DewNumberField has an onEditStart and no onEditEnd,
    // and that is a real difference rather than an oversight - a field holds
    // one transaction from its first change until it is next entered - so
    // asking about onEditStart would refuse the transport bar, which does that
    // correctly and with a flag named for the field it belongs to.
    //
    // DewKnob is the one exemption: it is what RAISES onEditEnd, from the
    // juce::Slider it owns.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            const auto trimmed = line.trim();

            return trimmed.startsWith ("onEditEnd =") || trimmed.contains (".onEditEnd =")
                   || trimmed.contains ("->onEditEnd =") || trimmed.contains (".onDragEnd =")
                   || trimmed.contains ("->onDragEnd =");
        },
        { "ui/primitives/RotaryGesture.h", "ui/primitives/DewKnob.cpp" });

    INFO ("the rotary gesture, written somewhere other than RotaryGesture:\n"
          << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no knob restates what its ParamSpec declared", "[build][gate][params]")
{
    // DewKnob's ParamSpec constructor applies the caption, the tooltip, the
    // range, the interval, the decimals and the bipolarity, and DewControls.h
    // says why: six knobs and a stepper stated those a second time by hand, and
    // every one of them had drifted from the engine's own clamp.
    //
    // Twenty-seven of them were still doing it - the oscillator section had
    // sixteen - and this time none had drifted. That is worth saying plainly:
    // they were all correct, and the reason to delete them is that being
    // correct today is what a restated number always is.
    //
    // Asks about a name ending in Knob, because that is what a DewKnob is
    // called wherever one is built from a spec. A DewNumberField takes no spec
    // and has to be told, so the fields in the transport bar, the render panel
    // and the rack row are not the shape this asks about.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            return line.contains ("Knob.setBipolar (")
                   || line.contains ("Knob.setNumDecimalPlaces (");
        },
        {});

    INFO ("a knob restating its own spec:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no test writes a mouse event or a component walk by hand", "[build][gate][tests]")
{
    // The first gate over the SUITE rather than over src/, and the two helpers
    // it guards were the argument for having one. Sixteen places built a
    // juce::MouseEvent from its fifteen-argument constructor, in ten files and
    // under four names - eventAt, eventOn, clickAt, and eight anonymous
    // lambdas - while TestSupport.h held the same body under a doc comment
    // claiming the job was done. Nine files then wrote findDescendantWithID,
    // byte-identical but for one parameter name.
    //
    // Neither was findable: one is a constructor call with no distinctive name
    // at all, and the other is four lines too short to look like duplication.
    // A gate is the only thing that sees either.
    //
    // The exemptions are the two definitions, and a harness may still wrap one
    // in its own name - RollHarness and PlaylistHarness take a Point<int>
    // because that is what their call sites hold. What they may not do is fill
    // the constructor again.
    //
    // And this file, which is the first gate over tests/ and therefore the
    // first that can read itself. Quoted literals are copied through whole by
    // the scanner - deliberately, so a marker a gate quotes cannot open a
    // comment - so the two needles below are two matches in this file.
    const auto found = offendersIn (
        testFiles(), juce::File { DEW_TESTS_DIR },
        [] (const juce::String& line)
        {
            return line.contains ("getMainMouseSource")
                   || line.contains ("juce::Component* findDescendantWithID (");
        },
        { "TestSupport.h", "ControlWalkHarness.h", "SourceGateOwnershipTests.cpp" });

    INFO ("a mouse event or a component walk written out again:\n" << found.joinIntoString ("\n"));
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
    //
    // ui/design/Keys.h is the fourth, and is the day that happened - though not
    // to this file. What a stroke IS moved down into dew_design when the
    // primitives had to answer keys of their own, taking keyPressFor with it, so
    // the one juce::KeyPress construction in the tree is now there. Only the
    // mechanism moved: Hotkeys.cpp still calls addDefaultKeypress, so both
    // exemptions still suppress a line and neither goes idle.
    const auto found = offenders (
        [] (const juce::String& line)
        {
            return line.contains ("addDefaultKeypress (") || line.contains ("juce::KeyPress (")
                   || line.contains ("KeyPress::createFromDescription");
        },
        { "ui/Hotkeys.cpp", "ui/design/Keys.h", "ui/ScoreEditorComponent.cpp" });

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

TEST_CASE ("every button dew declares refuses the right button", "[build][gate][gesture]")
{
    // This gate used to ask a different question: given a file that names
    // popupPress, does it name all three phases? Which meant it began with a
    // `continue` for every file that names it NOWHERE - so the two controls
    // that got this wrong were both invisible to it. The oscillator slot tabs
    // guarded the press by hand and not the drag or the release; the editor tab
    // bar was a stock juce::TabbedComponent whose buttons guarded nothing at
    // all, and TabBarButton::clicked re-reads the modifiers at RELEASE time, so
    // a ctrl-click whose ctrl came up first switched editor.
    //
    // A conditional gate cannot see the case it exists for. So this one asks a
    // SHAPE: deriving from a juce::Button type directly is opting out of the
    // rule, whatever the class then does. The one way in is PopupSafeButton,
    // which states the three phases and the click callback once.
    const juce::StringArray buttonTypes { "juce::Button",         "juce::TextButton",
                                          "juce::ToggleButton",   "juce::TabBarButton",
                                          "juce::DrawableButton", "juce::ShapeButton",
                                          "juce::ImageButton",    "juce::ArrowButton",
                                          "juce::HyperlinkButton" };

    juce::StringArray found;
    auto guarded = 0;

    for (const auto& file : sourceFiles())
    {
        for (const auto& line : codeLinesWithNumbersOf (file))
        {
            if (line.text.contains ("PopupSafeButton<"))
                ++guarded;

            if (! line.text.contains (": public juce::") && ! line.text.contains (":public juce::"))
                continue;

            for (const auto& type : buttonTypes)
                if (line.text.contains ("public " + type)
                    && ! line.text.contains ("PopupSafeButton"))
                    found.add (relativePathOf (file) + ":" + juce::String (line.number) + "  "
                               + type.trim() + " without the popup guard");
        }
    }

    // Control case: a scan that recognised no guarded button would pass in
    // silence, which is the failure mode the old gate actually had.
    INFO ("guarded button declarations seen: " << guarded);
    REQUIRE (guarded > 5);

    INFO ("buttons deriving from a raw juce type:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("a control that arms a popup press also disarms it", "[build][gate][gesture]")
{
    // Still worth stating for the controls that are NOT buttons and so cannot
    // take PopupSafeButton: a juce::Slider, and an effect card, both of which
    // hold the latch themselves. juce::Slider has the same hole with no guard
    // at all - it treats a right press as a menu only when setPopupMenuEnabled
    // is on, and nothing in dew turns it on, so the press fell into the drag
    // branch and moved the value.
    //
    // Not testable through the window for a button: Button::isMouseSourceOver
    // asks Component::isMouseOver, which reads the real pointer, and a headless
    // harness has none. What IS checkable is that the three phases were written
    // together.
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
    REQUIRE (arming >= 2);

    INFO ("controls that refuse only the press:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("no transport control writes the state the engine owns", "[build][gate][transport]")
{
    // AudioEngine is not a ChangeBroadcaster, and its transport and its mode
    // live in atomics rather than in the ValueTree - so nothing tells the
    // toolbar when something else moves them, and something else routinely
    // does: the Transport menu, a hotkey, an MCP client, the device going away.
    //
    // The answer is a single reader, TransportBar::refreshEngineState, polled
    // at the rate a transport reads at. What breaks it is a control that ALSO
    // writes its own appearance from its click handler: two writers, one of
    // which every other route bypasses. The mode button did exactly that, and
    // the only route anyone drove was the one route that also updated the
    // button, so cmd-L left a stale fill and a stale caption behind it.
    //
    // Asked as a SHAPE - a click handler in this translation unit that paints
    // itself - rather than as a list of buttons, so a sixth control added
    // tomorrow is covered without anybody remembering to add it.
    const juce::File file { juce::String (DEW_SOURCE_DIR) + "/ui/TransportBar.cpp" };
    REQUIRE (file.existsAsFile());

    const auto lines = codeLinesWithNumbersOf (file);

    juce::StringArray found;
    auto insideClickHandler = false;
    auto handlers = 0;

    for (const auto& line : lines)
    {
        const auto text = line.text.trim();

        if (text.contains (".onClick = [") || text.contains (".onModifiedClick = ["))
        {
            insideClickHandler = true;
            ++handlers;
            continue;
        }

        if (insideClickHandler && text.startsWith ("};"))
        {
            insideClickHandler = false;
            continue;
        }

        if (! insideClickHandler)
            continue;

        for (const auto* self : { "setToggleState (", "setButtonText (", "setIcon (" })
            if (text.contains (self))
                found.add (relativePathOf (file) + ":" + juce::String (line.number) + "  " + text);
    }

    // Control case: a gate that walked no handlers at all would pass in silence,
    // and this file has had a click handler on every transport control since it
    // was written.
    INFO ("click handlers walked: " << handlers);
    REQUIRE (handlers >= 4);

    INFO ("transport controls painting themselves from their own click:\n"
          << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}
