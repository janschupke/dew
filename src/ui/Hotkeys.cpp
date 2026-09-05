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
          StringId::command_fileNew_name, StringId::command_fileNew_description,
          StringId::command_category_file },
        { CommandIDs::fileOpen, { 'o', juce::ModifierKeys::commandModifier },
          StringId::command_fileOpen_name, StringId::command_fileOpen_description,
          StringId::command_category_file },
        { CommandIDs::fileSave, { 's', juce::ModifierKeys::commandModifier },
          StringId::command_fileSave_name, StringId::command_fileSave_description,
          StringId::command_category_file },
        { CommandIDs::fileSaveAs, { 's', juce::ModifierKeys::commandModifier
                                         | juce::ModifierKeys::shiftModifier },
          StringId::command_fileSaveAs_name, StringId::command_fileSaveAs_description,
          StringId::command_category_file },
        { CommandIDs::fileRender, { 'e', juce::ModifierKeys::commandModifier },
          StringId::command_fileRender_name, StringId::command_fileRender_description,
          StringId::command_category_file },

        { CommandIDs::editUndo, { 'z', juce::ModifierKeys::commandModifier },
          StringId::command_editUndo_name, StringId::command_editUndo_description,
          StringId::command_category_edit },
        { CommandIDs::editRedo, { 'z', juce::ModifierKeys::commandModifier
                                       | juce::ModifierKeys::shiftModifier },
          StringId::command_editRedo_name, StringId::command_editRedo_description,
          StringId::command_category_edit },

        // The digits are reachable at last. They were bound to the tools in
        // the timeline map, which matched them without looking at a modifier
        // and swallowed cmd-1 before it could get here.
        { CommandIDs::viewChannelRack, { '1', juce::ModifierKeys::commandModifier },
          StringId::command_viewChannelRack_name, StringId::command_viewChannelRack_description,
          StringId::command_category_view },
        { CommandIDs::viewPianoRoll, { '2', juce::ModifierKeys::commandModifier },
          StringId::command_viewPianoRoll_name, StringId::command_viewPianoRoll_description,
          StringId::command_category_view },
        { CommandIDs::viewPlaylist, { '3', juce::ModifierKeys::commandModifier },
          StringId::command_viewPlaylist_name, StringId::command_viewPlaylist_description,
          StringId::command_category_view },
        { CommandIDs::viewMixer, { '4', juce::ModifierKeys::commandModifier },
          StringId::command_viewMixer_name, StringId::command_viewMixer_description,
          StringId::command_category_view },
        { CommandIDs::viewScore, { '5', juce::ModifierKeys::commandModifier },
          StringId::command_viewScore_name, StringId::command_viewScore_description,
          StringId::command_category_view },
        { CommandIDs::viewNextTab, { juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier },
          StringId::command_viewNextTab_name, StringId::command_viewNextTab_description,
          StringId::command_category_view },
        { CommandIDs::viewPreviousTab, { juce::KeyPress::tabKey,
                                         juce::ModifierKeys::ctrlModifier
                                         | juce::ModifierKeys::shiftModifier },
          StringId::command_viewPreviousTab_name, StringId::command_viewPreviousTab_description,
          StringId::command_category_view },
        { CommandIDs::viewToggleInstrumentPanel, { '\\', juce::ModifierKeys::commandModifier },
          StringId::command_viewToggleInstrumentPanel_name, StringId::command_viewToggleInstrumentPanel_description,
          StringId::command_category_view },

        // The first rows with no key. A scale is set once and then left alone,
        // and a shortcut for it would be four more keys spent on something
        // nobody presses twice. An empty Stroke is how a row says so, and
        // describe() adds no default keypress for one.
        { CommandIDs::viewUiScaleFirst, {},
          StringId::command_viewUiScaleFirst_name, StringId::command_viewUiScaleFirst_description,
          StringId::command_category_view },
        { CommandIDs::viewUiScale125, {},
          StringId::command_viewUiScale125_name, StringId::command_viewUiScale125_description,
          StringId::command_category_view },
        { CommandIDs::viewUiScale150, {},
          StringId::command_viewUiScale150_name, StringId::command_viewUiScale150_description,
          StringId::command_category_view },
        { CommandIDs::viewUiScale175, {},
          StringId::command_viewUiScale175_name, StringId::command_viewUiScale175_description,
          StringId::command_category_view },

        // Keyless for the same reason the scales are. Reduce motion was built,
        // persisted and honoured everywhere, and had no way to be turned on:
        // it lived in the settings file and nowhere a person could reach.
        { CommandIDs::viewMotionFirst, {},
          StringId::command_viewMotionFirst_name, StringId::command_viewMotionFirst_description,
          StringId::command_category_view },
        { CommandIDs::viewMotionFull, {},
          StringId::command_viewMotionFull_name, StringId::command_viewMotionFull_description,
          StringId::command_category_view },
        { CommandIDs::viewMotionReduced, {},
          StringId::command_viewMotionReduced_name, StringId::command_viewMotionReduced_description,
          StringId::command_category_view },

        // Keyless, like the scales and the motion settings: a theme is chosen
        // once and then left alone.
        { CommandIDs::viewThemeFirst, {},
          StringId::command_viewThemeFirst_name, StringId::command_viewThemeFirst_description,
          StringId::command_category_view },
        { CommandIDs::viewThemeHighContrast, {},
          StringId::command_viewThemeHighContrast_name, StringId::command_viewThemeHighContrast_description,
          StringId::command_category_view },

        { CommandIDs::transportPlayStop, { juce::KeyPress::spaceKey, 0 },
          StringId::command_transportPlayStop_name, StringId::command_transportPlayStop_description,
          StringId::command_category_transport },
        { CommandIDs::transportRewind, { juce::KeyPress::homeKey, 0 },
          StringId::command_transportRewind_name, StringId::command_transportRewind_description,
          StringId::command_category_transport },
        // Bare `r`, and it stays bare: the piano roll used to bind the same key
        // to its randomize dialog and shadow this whenever the roll had focus.
        // Randomize moved to shift-r; recording is the one that has to work
        // from wherever you happen to be looking.
        { CommandIDs::transportRecord, { 'r', 0 },
          StringId::command_transportRecord_name, StringId::command_transportRecord_description,
          StringId::command_category_transport },
        { CommandIDs::transportToggleMode, { 'l', juce::ModifierKeys::commandModifier },
          StringId::command_transportToggleMode_name, StringId::command_transportToggleMode_description,
          StringId::command_category_transport },

        { CommandIDs::addChannel, { 'k', juce::ModifierKeys::commandModifier },
          StringId::command_addChannel_name, StringId::command_addChannel_description,
          StringId::command_category_project },
        { CommandIDs::addPattern, { 'p', juce::ModifierKeys::commandModifier
                                         | juce::ModifierKeys::shiftModifier },
          StringId::command_addPattern_name, StringId::command_addPattern_description,
          StringId::command_category_project },
        { CommandIDs::compileScore, { 'r', juce::ModifierKeys::commandModifier },
          StringId::command_compileScore_name, StringId::command_compileScore_description,
          StringId::command_category_project },

        // Keyless since Preferences took cmd-comma, which is where every
        // platform's convention says the settings window lives. The menu item
        // stays: this dialog is still the place the device is chosen, and
        // Preferences shows the very same panel on its Audio page.
        { CommandIDs::audioSettings, Stroke {}, StringId::command_audioSettings_name,
          StringId::command_audioSettings_description, StringId::command_category_audio },
        { CommandIDs::midiSettings, { ',', juce::ModifierKeys::commandModifier
                                           | juce::ModifierKeys::shiftModifier },
          StringId::command_midiSettings_name, StringId::command_midiSettings_description,
          StringId::command_category_audio },

        // No key. Every stroke a person could reach for here is either taken or
        // worth more to something they do often, and this is a switch somebody
        // sets once - describe() skips addDefaultKeypress for keyCode 0, and a
        // keyless row collides with nothing.
        { CommandIDs::mcpSettings, Stroke {}, StringId::command_mcpSettings_name,
          StringId::command_mcpSettings_description, StringId::command_category_tools },

        // The one stroke this table would be wrong to spell any other way.
        { CommandIDs::preferences, { ',', juce::ModifierKeys::commandModifier },
          StringId::command_preferences_name, StringId::command_preferences_description,
          StringId::command_category_application },
        { CommandIDs::about, Stroke {}, StringId::command_about_name,
          StringId::command_about_description, StringId::command_category_application },
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
        { ViewCommand::zoomIn, { '=', 0 }, StringId::command_zoomIn_name, StringId::command_zoomIn_description,
          StringId::command_category_view },
        { ViewCommand::zoomIn, { '+', 0 }, StringId::command_zoomIn_name, StringId::command_zoomIn_description,
          StringId::command_category_view },
        { ViewCommand::zoomOut, { '-', 0 }, StringId::command_zoomOut_name, StringId::command_zoomOut_description,
          StringId::command_category_view },
        { ViewCommand::zoomOut, { '_', 0 }, StringId::command_zoomOut_name, StringId::command_zoomOut_description,
          StringId::command_category_view },
        { ViewCommand::zoomToFit, { '0', 0 }, StringId::command_zoomToFit_name, StringId::command_zoomToFit_description,
          StringId::command_category_view },

        { ViewCommand::selectTool, { '1', 0 }, StringId::command_selectTool_name, StringId::command_selectTool_description,
          StringId::command_category_tools },
        { ViewCommand::paintTool, { '2', 0 }, StringId::command_paintTool_name, StringId::command_paintTool_description,
          StringId::command_category_tools },
        { ViewCommand::eraseTool, { '3', 0 }, StringId::command_eraseTool_name, StringId::command_eraseTool_description,
          StringId::command_category_tools },

        { ViewCommand::clearSelection, { juce::KeyPress::escapeKey, 0 },
          StringId::command_clearSelection_name, StringId::command_clearSelection_description,
          StringId::command_category_edit },
        { ViewCommand::deleteSelection, { juce::KeyPress::deleteKey, 0 },
          StringId::command_deleteSelection_name, StringId::command_deleteSelection_description,
          StringId::command_category_edit },
        { ViewCommand::deleteSelection, { juce::KeyPress::backspaceKey, 0 },
          StringId::command_deleteSelection_name, StringId::command_deleteSelection_description,
          StringId::command_category_edit },

        // Command OR control. It was command-only in the piano roll, so it did
        // nothing on a machine driven with control even though rubber-band
        // select on the same modifier worked.
        { ViewCommand::selectAll, { 'a', juce::ModifierKeys::commandModifier },
          StringId::command_selectAll_name, StringId::command_selectAll_description,
          StringId::command_category_edit },
        { ViewCommand::selectAll, { 'a', juce::ModifierKeys::ctrlModifier },
          StringId::command_selectAll_name, StringId::command_selectAll_description,
          StringId::command_category_edit },

        // The same three keys as zoom, on alt: the other axis, reached the same
        // way. Alt rather than command because command-digit belongs to the tab
        // switcher, and a modified digit reaching a timeline view is the exact
        // bug the two tables were merged to fix.
        { ViewCommand::sizeBigger, { '=', juce::ModifierKeys::altModifier },
          StringId::command_sizeBigger_name, StringId::command_sizeBigger_description,
          StringId::command_category_view },
        { ViewCommand::sizeBigger, { '+', juce::ModifierKeys::altModifier },
          StringId::command_sizeBigger_name, StringId::command_sizeBigger_description,
          StringId::command_category_view },
        { ViewCommand::sizeSmaller, { '-', juce::ModifierKeys::altModifier },
          StringId::command_sizeSmaller_name, StringId::command_sizeSmaller_description,
          StringId::command_category_view },
        { ViewCommand::sizeSmaller, { '_', juce::ModifierKeys::altModifier },
          StringId::command_sizeSmaller_name, StringId::command_sizeSmaller_description,
          StringId::command_category_view },
        { ViewCommand::sizeDefault, { '0', juce::ModifierKeys::altModifier },
          StringId::command_sizeDefault_name, StringId::command_sizeDefault_description,
          StringId::command_category_view },

        // Bare, and reaching keyPressed only when the canvas itself has focus -
        // so they cannot collide with typing into a number field.
        { ViewCommand::cursorLeft, { juce::KeyPress::leftKey, 0 },
          StringId::command_cursorLeft_name, StringId::command_cursorLeft_description,
          StringId::command_category_edit },
        { ViewCommand::cursorRight, { juce::KeyPress::rightKey, 0 },
          StringId::command_cursorRight_name, StringId::command_cursorRight_description,
          StringId::command_category_edit },
        { ViewCommand::cursorUp, { juce::KeyPress::upKey, 0 },
          StringId::command_cursorUp_name, StringId::command_cursorUp_description,
          StringId::command_category_edit },
        { ViewCommand::cursorDown, { juce::KeyPress::downKey, 0 },
          StringId::command_cursorDown_name, StringId::command_cursorDown_description,
          StringId::command_category_edit },
        { ViewCommand::cursorActivate, { juce::KeyPress::returnKey, 0 },
          StringId::command_cursorActivate_name, StringId::command_cursorActivate_description,
          StringId::command_category_edit },
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

    info.setInfo (tr (binding->name), tr (binding->description), tr (binding->category), 0);

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
