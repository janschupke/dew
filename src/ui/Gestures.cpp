#include "ui/Gestures.h"

namespace dew::gesture
{

Command commandFor (const juce::KeyPress& key) noexcept
{
    // The printable keys are read as CHARACTERS where there is one: pressing
    // the key with '+' printed on it reports '=' as its CODE on most layouts,
    // and a user should not have to know that.
    //
    // Falling back to the code matters as much. A KeyPress built from a code
    // alone carries no text character - which is every KeyPress a test makes,
    // and which is why this map had to be reachable both ways to be testable
    // at all.
    const auto typed = key.getTextCharacter();
    const auto character = typed != 0 ? typed : (juce::juce_wchar) key.getKeyCode();
    const auto mods = key.getModifiers();

    if ((character == 'a' || character == 'A') && (mods.isCommandDown() || mods.isCtrlDown()))
        return Command::selectAll;

    if (character == '+' || character == '=')  return Command::zoomIn;
    if (character == '-' || character == '_')  return Command::zoomOut;
    if (character == '0')                      return Command::zoomToFit;

    if (character == '1') return Command::selectTool;
    if (character == '2') return Command::paintTool;
    if (character == '3') return Command::eraseTool;

    if (key.getKeyCode() == juce::KeyPress::escapeKey)
        return Command::clearSelection;

    if (key.getKeyCode() == juce::KeyPress::deleteKey
        || key.getKeyCode() == juce::KeyPress::backspaceKey)
        return Command::deleteSelection;

    return Command::none;
}

} // namespace dew::gesture
