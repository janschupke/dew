#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"
#include "ui/design/Keys.h"

#include <vector>

namespace dew
{

/** The application commands, by id.

    Kept as a bare enum of juce::CommandID values because that is what
    ApplicationCommandManager takes. What each one is CALLED and which key
    reaches it lives next door in hotkeys::application(), not here.
*/
namespace CommandIDs
{

enum
{
    fileNew = 0x2000,
    fileOpen,
    fileSave,
    fileSaveAs,
    fileRender,
    editUndo,
    editRedo,
    transportPlayStop,
    transportRewind,
    transportToggleMode,
    transportRecord,
    transportPanic,

    /** The two switches beside the transport, rather than actions on it.

        Placed in the transport run and well above every contiguous block below
        - the scale, motion and theme ids are read as First + step - so nothing
        that counts on an offset moves. `about` is still last, which is what
        HotkeyTests walks the table to.
    */
    transportMetronome,
    transportKeyboardInput,
    addChannel,
    addPattern,
    compileScore,
    audioSettings,
    midiSettings,
    mcpSettings,
    viewChannelRack,
    viewPianoRoll,
    viewPlaylist,
    viewMixer,
    viewScore,
    viewNextTab,
    viewPreviousTab,

    /** Moving the keyboard over a whole component - an effect, a strip, a
        panel - rather than one control at a time. Placed here, in the middle of
        the View run, because `about` has to stay LAST: HotkeyTests pins
        lastCommand to it and walks the table from fileNew to there. The
        contiguous scale, motion and theme blocks are all below this and read
        their ids as First + step, so inserting above them moves nothing. */
    viewNextGroup,
    viewPreviousGroup,
    viewToggleInstrumentPanel,

    /** One per Settings::uiScaleSteps, contiguous and in the same order, so the
        menu can loop over the steps rather than name each id twice. */
    viewUiScaleFirst,
    viewUiScale125 = viewUiScaleFirst + 1,
    viewUiScale150,
    viewUiScale175,

    /** One per Settings::Motion, contiguous and in the same order, so the menu
        loops rather than naming each id twice - the way the scales do. */
    viewMotionFirst,
    viewMotionFull = viewMotionFirst + 1,
    viewMotionReduced,

    /** One per theme::Kind, contiguous and in the same order, so the menu loops
        rather than naming each id twice - the way the scales and the motion
        settings do. */
    viewThemeFirst,
    viewThemeHighContrast = viewThemeFirst + 1,

    /** The two that are not in a menu on every platform.

        macOS puts both in the application menu, which is not on the bar at all
        - so they are ids the command manager knows and the bar may or may not
        show, which is exactly what a command id is for.

        `about` is LAST, and HotkeyTests holds its lastCommand against it: the
        table is walked from fileNew to here, so an id appended after this one
        without moving that constant is a row the drift gate stops checking.
    */
    preferences,
    about,
};

} // namespace CommandIDs

/** Every key dew binds, declared once.

    There used to be two key tables that did not know about each other: fifteen
    addDefaultKeypress calls written inline in DewApplication::getCommandInfo,
    and a separate if-chain in Gestures.cpp for the three timeline views. Three
    things followed from that, and all three were invisible from inside either
    half:

      - The timeline map matched bare DIGITS while ignoring modifiers, so cmd-1
        resolved to the paint tool in whichever view had focus and was consumed
        there. No mod-plus-number scheme could ever have reached the window.
      - Bare `r` meant Record to the application and Randomize to the piano
        roll, and whichever had focus won.
      - DewApplication.cpp belongs to no library, so nothing could test the
        application table at all.

    An explicit table joined to the enum, in the shape ModuleCatalog and
    AutomationTargets already use - deliberately not self-registration. These
    are static libraries built without --whole-archive, so a `static Registrar`
    in some translation unit would be dropped by the linker and the binding
    would vanish with no error anywhere.

    It lives in dew_ui rather than beside the application for exactly the third
    reason above: dew_tests links dew_ui.

    What a stroke IS moved one layer further down, to ui/design/Keys.h, when the
    primitives had to answer keys of their own - a knob cannot see dew_ui. Only
    the mechanism went: this table names CommandIDs::compileScore, addChannel and
    fileRender, and dew_design's own CMakeLists says it "knows nothing about a
    project". So Keys.h says what a stroke is, this file declares the
    application's rows, and Keys.h declares the design system's own.
*/
namespace hotkeys
{

/** The mechanism, one layer down. Aliased rather than re-declared so that
    nothing which already spells hotkeys::Stroke had to change. */
using Stroke = keys::Stroke;

template <typename Action> using Binding = keys::Binding<Action>;

using keys::keyPressFor;
using keys::matches;

/** What a key means in a timeline view.

    A separate enum from the command ids on purpose. The three views switch
    over it with no `default:`, so adding a command here is a compile error in
    the step grid, the piano roll and the playlist until each one says what it
    does about it - and a single merged enum would make every view answer for
    Save As too.
*/
enum class ViewCommand
{
    none,
    zoomIn,
    zoomOut,
    zoomToFit,
    selectTool,
    paintTool,
    eraseTool,
    clearSelection,
    deleteSelection,
    selectAll,

    /** The view's OTHER size: the one zoom does not reach.

        Zoom is horizontal in all three timelines, so nothing bound the axis a
        lane is measured on. In the playlist that is lane height, in the piano
        roll the height of a pitch row, and in the score tab - which has no
        second axis because it has no timeline - the size of the text.

        One trio rather than two, because it is one idea. A key that means
        something in two views means the same thing in both; the map is not a
        promise that every view has every command, and the step grid has no
        second size to give.

        Alt-modified, where zoom is bare: `=` typed into the score document has
        to arrive as an `=`, and matches() compares alt exactly.
    */
    sizeBigger,
    sizeSmaller,
    sizeDefault,

    /** Moving the keyboard's own position on a canvas that paints its contents.

        The three timelines draw their notes, clips and cells rather than
        parenting them, so until this there was nothing for the keyboard to land
        on: every one of them could only be edited with a mouse, and a screen
        reader met a rectangle with a name and no contents.

        What an axis MEANS is the view's: a step and a pitch in the roll, a bar
        and a track in the playlist, a step and a channel in the grid. What they
        share is that left and right move along time, which is the axis all
        three have.

        Bare arrows, because this is the most ordinary thing a person can ask a
        canvas to do and it should not need a modifier. The piano roll's
        transpose, which had them, moves to alt - the modifier dew already uses
        for a view's other axis.
    */
    cursorLeft,
    cursorRight,
    cursorUp,
    cursorDown,

    /** Do the thing under the cursor: toggle a step, select a note, open a
        clip's pattern. Return rather than space, which is Play everywhere and
        has to stay that way. */
    cursorActivate,
};

/** The menu-bar commands, in the order the menus present them. */
const std::vector<Binding<juce::CommandID>>& application();

/** The bindings the step grid, the piano roll and the playlist share. */
const std::vector<Binding<ViewCommand>>& timeline();

/** The strokes a VALUE control answers - a knob, a fader, a stepper, a number
    field - declared one layer down in ui/design/Keys.h because that is where
    the controls are, and re-exported here so the collision test and the docs
    walk ONE registry rather than two. That was the whole point of this file. */
const std::vector<Binding<keys::valueKeys::Command>>& value();

/** The row for one command id, or null. */
const Binding<juce::CommandID>* find (juce::CommandID);

/** Fills in everything about a command that is a constant - what it is called,
    which menu it belongs to, and the key that reaches it - and returns false
    for an id with no row.

    The caller is left holding setActive, which is the only part of a command
    that is not a constant. Written here rather than at the call site so that
    NOTHING outside this file names a key, which is what the source gate can
    then say.
*/
bool describe (juce::CommandID, juce::ApplicationCommandInfo&);

/** What a key means in a timeline view, or ViewCommand::none. */
ViewCommand viewCommandFor (const juce::KeyPress&) noexcept;

} // namespace hotkeys

} // namespace dew
