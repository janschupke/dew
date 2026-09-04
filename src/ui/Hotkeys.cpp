#include "ui/Hotkeys.h"

namespace dew::hotkeys
{

namespace
{

/** The modifiers a binding is allowed to be picky about.

// clang-format off
    Shift is absent, and that is the rule rather than an omission - see
    matches() in the header.
*/
constexpr int comparedModifiers = juce::ModifierKeys::commandModifier
                                  | juce::ModifierKeys::ctrlModifier
                                  | juce::ModifierKeys::altModifier;

// clang-format on
int lowerCase (int character) noexcept
{
    return (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) character);
}

} // namespace

// clang-format off
const std::vector<Binding<juce::CommandID>>& application()
{
    // A function-local static rather than a namespace-scope array: the
    // KeyPress constants are static const ints, not constants a static
    // initialiser can read in a defined order.
    //
    // In menu order, so the menu bar is a view of this rather than a second
    // list that has to be kept in step with it.
    static const std::vector<Binding<juce::CommandID>> table
    {
        { CommandIDs::fileNew, { 'n', juce::ModifierKeys::commandModifier },
          "New", "Start an empty project", "File" },
        { CommandIDs::fileOpen, { 'o', juce::ModifierKeys::commandModifier },
          "Open...", "Open a dew project", "File" },
        { CommandIDs::fileSave, { 's', juce::ModifierKeys::commandModifier },
          "Save", "Save this project", "File" },
        { CommandIDs::fileSaveAs, { 's', juce::ModifierKeys::commandModifier
                                         | juce::ModifierKeys::shiftModifier },
          "Save As...", "Save this project to a new file", "File" },
        { CommandIDs::fileRender, { 'e', juce::ModifierKeys::commandModifier },
          "Render...", "Write this project out as audio or MIDI", "File" },

        { CommandIDs::editUndo, { 'z', juce::ModifierKeys::commandModifier },
          "Undo", "Undo the last edit", "Edit" },
        { CommandIDs::editRedo, { 'z', juce::ModifierKeys::commandModifier
                                       | juce::ModifierKeys::shiftModifier },
          "Redo", "Redo the last undone edit", "Edit" },

        // The digits are reachable at last. They were bound to the tools in
        // the timeline map, which matched them without looking at a modifier
        // and swallowed cmd-1 before it could get here.
        { CommandIDs::viewChannelRack, { '1', juce::ModifierKeys::commandModifier },
          "Channel Rack", "Show the channel rack", "View" },
        { CommandIDs::viewPianoRoll, { '2', juce::ModifierKeys::commandModifier },
          "Piano Roll", "Show the piano roll", "View" },
        { CommandIDs::viewPlaylist, { '3', juce::ModifierKeys::commandModifier },
          "Playlist", "Show the playlist", "View" },
        { CommandIDs::viewMixer, { '4', juce::ModifierKeys::commandModifier },
          "Mixer", "Show the mixer", "View" },
        { CommandIDs::viewScore, { '5', juce::ModifierKeys::commandModifier },
          "Score", "Show the score", "View" },
        { CommandIDs::viewNextTab, { juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier },
          "Next Editor", "Move to the next editor", "View" },
        { CommandIDs::viewPreviousTab, { juce::KeyPress::tabKey,
                                         juce::ModifierKeys::ctrlModifier
                                         | juce::ModifierKeys::shiftModifier },
          "Previous Editor", "Move to the previous editor", "View" },
        { CommandIDs::viewToggleInstrumentPanel, { '\\', juce::ModifierKeys::commandModifier },
          "Show / Hide Instrument Panel", "Fold the instrument panel away, or bring it back",
          "View" },

        // The first rows with no key. A scale is set once and then left alone,
        // and a shortcut for it would be four more keys spent on something
        // nobody presses twice. An empty Stroke is how a row says so, and
        // describe() adds no default keypress for one.
        { CommandIDs::viewUiScaleFirst, {},
          "100%", "Draw the interface at its original size", "View" },
        { CommandIDs::viewUiScale125, {},
          "125%", "Draw the interface a quarter larger", "View" },
        { CommandIDs::viewUiScale150, {},
          "150%", "Draw the interface half again as large", "View" },
        { CommandIDs::viewUiScale175, {},
          "175%", "Draw the interface three quarters larger", "View" },

        // Keyless for the same reason the scales are. Reduce motion was built,
        // persisted and honoured everywhere, and had no way to be turned on:
        // it lived in the settings file and nowhere a person could reach.
        { CommandIDs::viewMotionFirst, {},
          "Follow the system", "Reduce motion when the operating system asks for it", "View" },
        { CommandIDs::viewMotionFull, {},
          "Full motion", "Animate transitions, whatever the system prefers", "View" },
        { CommandIDs::viewMotionReduced, {},
          "Reduce motion", "Make every transition instant", "View" },

        // Keyless, like the scales and the motion settings: a theme is chosen
        // once and then left alone.
        { CommandIDs::viewThemeFirst, {},
          "Default", "dew's own palette", "View" },
        { CommandIDs::viewThemeHighContrast, {},
          "High contrast", "The same palette pushed apart, for reading at distance", "View" },

        { CommandIDs::transportPlayStop, { juce::KeyPress::spaceKey, 0 },
          "Play / Stop", "Start or stop playback", "Transport" },
        { CommandIDs::transportRewind, { juce::KeyPress::homeKey, 0 },
          "Rewind", "Return the playhead to the start", "Transport" },
        // Bare `r`, and it stays bare: the piano roll used to bind the same key
        // to its randomize dialog and shadow this whenever the roll had focus.
        // Randomize moved to shift-r; recording is the one that has to work
        // from wherever you happen to be looking.
        { CommandIDs::transportRecord, { 'r', 0 },
          "Record", "Record audio into the armed channel", "Transport" },
        { CommandIDs::transportToggleMode, { 'l', juce::ModifierKeys::commandModifier },
          "Toggle Pattern / Song", "Switch between pattern and song playback", "Transport" },

        { CommandIDs::addChannel, { 'k', juce::ModifierKeys::commandModifier },
          "Add Channel", "Add a new instrument channel", "Project" },
        { CommandIDs::addPattern, { 'p', juce::ModifierKeys::commandModifier
                                         | juce::ModifierKeys::shiftModifier },
          "Add Pattern", "Add a new pattern", "Project" },
        { CommandIDs::compileScore, { 'r', juce::ModifierKeys::commandModifier },
          "Compile Score", "Turn the score into notes in this project", "Project" },

        { CommandIDs::audioSettings, { ',', juce::ModifierKeys::commandModifier },
          "Audio Settings...", "Choose the audio device, sample rate and buffer size", "Audio" },
        { CommandIDs::midiSettings, { ',', juce::ModifierKeys::commandModifier
                                           | juce::ModifierKeys::shiftModifier },
          "MIDI Settings...", "Choose which MIDI controllers play", "Audio" },
    };

    // clang-format on
    return table;
}

// clang-format off
const std::vector<Binding<ViewCommand>>& timeline()
{
    static const std::vector<Binding<ViewCommand>> table
    {
        // `+` is shift-`=` on most layouts, so the key with `+` printed on it
        // reports `=` as its code. Both spellings are rows: a user should not
        // have to know which one their keyboard sends.
        { ViewCommand::zoomIn, { '=', 0 }, "Zoom In", "Zoom the timeline in", "View" },
        { ViewCommand::zoomIn, { '+', 0 }, "Zoom In", "Zoom the timeline in", "View" },
        { ViewCommand::zoomOut, { '-', 0 }, "Zoom Out", "Zoom the timeline out", "View" },
        { ViewCommand::zoomOut, { '_', 0 }, "Zoom Out", "Zoom the timeline out", "View" },
        { ViewCommand::zoomToFit, { '0', 0 }, "Zoom to Fit", "Fit the material to the view",
          "View" },

        { ViewCommand::selectTool, { '1', 0 }, "Select Tool", "Select and move", "Tools" },
        { ViewCommand::paintTool, { '2', 0 }, "Paint Tool", "Draw notes or clips", "Tools" },
        { ViewCommand::eraseTool, { '3', 0 }, "Slice Tool", "Slice notes", "Tools" },

        { ViewCommand::clearSelection, { juce::KeyPress::escapeKey, 0 },
          "Clear Selection", "Drop the current selection", "Edit" },
        { ViewCommand::deleteSelection, { juce::KeyPress::deleteKey, 0 },
          "Delete Selection", "Delete what is selected", "Edit" },
        { ViewCommand::deleteSelection, { juce::KeyPress::backspaceKey, 0 },
          "Delete Selection", "Delete what is selected", "Edit" },

        // Command OR control. It was command-only in the piano roll, so it did
        // nothing on a machine driven with control even though rubber-band
        // select on the same modifier worked.
        { ViewCommand::selectAll, { 'a', juce::ModifierKeys::commandModifier },
          "Select All", "Select everything on the channel", "Edit" },
        { ViewCommand::selectAll, { 'a', juce::ModifierKeys::ctrlModifier },
          "Select All", "Select everything on the channel", "Edit" },

        // The same three keys as zoom, on alt: the other axis, reached the same
        // way. Alt rather than command because command-digit belongs to the tab
        // switcher, and a modified digit reaching a timeline view is the exact
        // bug the two tables were merged to fix.
        { ViewCommand::sizeBigger, { '=', juce::ModifierKeys::altModifier },
          "Taller", "Make this view's rows, or its text, one size larger", "View" },
        { ViewCommand::sizeBigger, { '+', juce::ModifierKeys::altModifier },
          "Taller", "Make this view's rows, or its text, one size larger", "View" },
        { ViewCommand::sizeSmaller, { '-', juce::ModifierKeys::altModifier },
          "Shorter", "Make this view's rows, or its text, one size smaller", "View" },
        { ViewCommand::sizeSmaller, { '_', juce::ModifierKeys::altModifier },
          "Shorter", "Make this view's rows, or its text, one size smaller", "View" },
        { ViewCommand::sizeDefault, { '0', juce::ModifierKeys::altModifier },
          "Default Size", "Return this view's rows, or its text, to the default size", "View" },

        // Bare, and reaching keyPressed only when the canvas itself has focus -
        // so they cannot collide with typing into a number field.
        { ViewCommand::cursorLeft, { juce::KeyPress::leftKey, 0 },
          "Previous Step", "Move the keyboard's position earlier", "Edit" },
        { ViewCommand::cursorRight, { juce::KeyPress::rightKey, 0 },
          "Next Step", "Move the keyboard's position later", "Edit" },
        { ViewCommand::cursorUp, { juce::KeyPress::upKey, 0 },
          "Up", "Move the keyboard's position up a row", "Edit" },
        { ViewCommand::cursorDown, { juce::KeyPress::downKey, 0 },
          "Down", "Move the keyboard's position down a row", "Edit" },
        { ViewCommand::cursorActivate, { juce::KeyPress::returnKey, 0 },
          "Activate", "Act on what the keyboard is pointing at", "Edit" },
    };

    // clang-format on
    return table;
}

const Binding<juce::CommandID>* find (juce::CommandID id)
{
    for (const auto& binding : application())
        if (binding.action == id)
            return &binding;

    return nullptr;
}

bool describe (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto* binding = find (id);

    if (binding == nullptr)
        return false;

    info.setInfo (binding->name, binding->description, binding->category, 0);

    // A row with no key code is a menu item and nothing more. Passing 0 through
    // would register a KeyPress for key code zero, which every unhandled key
    // event compares equal to.
    if (binding->stroke.keyCode != 0)
        info.addDefaultKeypress (binding->stroke.keyCode,
                                 juce::ModifierKeys (binding->stroke.modifiers));

    return true;
}

juce::KeyPress keyPressFor (const Stroke& stroke)
{
    return juce::KeyPress (stroke.keyCode, juce::ModifierKeys (stroke.modifiers), 0);
}

bool matches (const Stroke& stroke, const juce::KeyPress& key) noexcept
{
    if ((key.getModifiers().getRawFlags() & comparedModifiers)
        != (stroke.modifiers & comparedModifiers))
        return false;

    // The code first, because a KeyPress built from a code alone carries no
    // text character - which is every KeyPress a test makes, and which is why
    // the map this replaced had to be reachable both ways to be testable at
    // all. Case-insensitively, because a letter pressed with a modifier
    // reports upper case on some layouts and lower on others.
    if (lowerCase (key.getKeyCode()) == lowerCase (stroke.keyCode))
        return true;

    const auto typed = (int) key.getTextCharacter();

    return typed != 0 && lowerCase (typed) == lowerCase (stroke.keyCode);
}

ViewCommand viewCommandFor (const juce::KeyPress& key) noexcept
{
    for (const auto& binding : timeline())
        if (matches (binding.stroke, key))
            return binding.action;

    return ViewCommand::none;
}

} // namespace dew::hotkeys
