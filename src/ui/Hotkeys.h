#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "i18n/Strings.h"

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
    addChannel,
    addPattern,
    compileScore,
    audioSettings,
    midiSettings,
    viewChannelRack,
    viewPianoRoll,
    viewPlaylist,
    viewMixer,
    viewScore,
    viewNextTab,
    viewPreviousTab,
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
*/
namespace hotkeys
{

/** One key, spelled once. `keyCode` is a juce::KeyPress code or a character;
    `modifiers` is a juce::ModifierKeys flag set.

    An action reachable two ways - select-all answers to command AND to control
    - gets two rows rather than a second field. Two rows say which two keys
    they are; a field would have to say how the two relate.
*/
struct Stroke
{
    int keyCode = 0;
    int modifiers = 0;
};

template <typename Action> struct Binding
{
    Action action;
    Stroke stroke;

    /** What this command is CALLED, what it does, and which menu it belongs
        to - as catalogue keys rather than sentences, so the menu bar and the
        command manager read the same row in whatever language is running. */
    StringId name;
    StringId description;
    StringId category;
};

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

/** The stroke as a KeyPress - the ONE place a KeyPress is constructed. */
juce::KeyPress keyPressFor (const Stroke&);

/** Whether a key press is this stroke.

    Command, control and alt are compared EXACTLY. That is the whole fix for
    cmd-1: the map this replaced compared a character and nothing else, so
    every modified digit resolved to a tool.

    Shift is deliberately NOT compared. `+` and `_` are how a keyboard spells
    shift-`=` and shift-`-`, and no binding here is distinguished by shift, so
    a map that compared it would refuse the one key it most has to accept. A
    binding that ever needs shift changes this rule and says so in its row.
*/
bool matches (const Stroke&, const juce::KeyPress&) noexcept;

/** What a key means in a timeline view, or ViewCommand::none. */
ViewCommand viewCommandFor (const juce::KeyPress&) noexcept;

} // namespace hotkeys

} // namespace dew
