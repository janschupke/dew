#include "ui/EditorTools.h"

#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

/** The key each tool answers to, in ONE place.

    The two strips each mapped this in their own keyPressed switch, and the
    playlist's was missing a row - so the slice key was neither handled nor
    passed on there, which is a key that does nothing for a reason no reader of
    either file could see.
*/
hotkeys::ViewCommand commandFor (EditorTool tool) noexcept
{
    switch (tool)
    {
        case EditorTool::select: return hotkeys::ViewCommand::selectTool;
        case EditorTool::paint: return hotkeys::ViewCommand::paintTool;
        case EditorTool::slice: return hotkeys::ViewCommand::eraseTool;
    }

    return hotkeys::ViewCommand::none;
}

} // namespace

ToolStrip::ToolStrip (juce::Component& owner, std::vector<Entry> rows)
    : entries (std::move (rows))
{
    for (const auto& entry : entries)
    {
        auto button = std::make_unique<DewIconButton> (entry.icon, entry.help);

        button->setClickingTogglesState (true);
        button->setRadioGroupId (1);

        const auto which = entry.tool;
        button->onClick = [this, which] { setTool (which); };

        // Without this a click on a tool moves focus off the canvas, and the
        // shortcuts it owns stop working until it is clicked again.
        button->setMouseClickGrabsKeyboardFocus (false);

        owner.addAndMakeVisible (*button);
        buttons.push_back (std::move (button));
    }

    // Whatever the first entry is, rather than assuming select: a strip that
    // did not offer the tool it started on would open showing none.
    if (! entries.empty())
        tool = entries.front().tool;

    updateButtons();
}

void ToolStrip::setTool (EditorTool newTool, juce::NotificationType notification)
{
    if (! offers (newTool))
        return;

    if (tool == newTool)
    {
        // The radio group can clear the button of the tool that is already
        // current when it is clicked again; put it back rather than leaving the
        // strip showing no tool at all.
        updateButtons();
        return;
    }

    tool = newTool;
    updateButtons();

    if (notification != juce::dontSendNotification && onToolChanged)
        onToolChanged();
}

bool ToolStrip::offers (EditorTool wanted) const noexcept
{
    for (const auto& entry : entries)
        if (entry.tool == wanted)
            return true;

    return false;
}

bool ToolStrip::applyCommand (hotkeys::ViewCommand command)
{
    for (const auto& entry : entries)
    {
        if (commandFor (entry.tool) != command)
            continue;

        setTool (entry.tool);
        return true;
    }

    return false;
}

void ToolStrip::placeAll (const std::function<void (juce::Component&, int)>& place) const
{
    for (const auto& button : buttons)
        place (*button, tokens::size::iconButton);
}

int ToolStrip::preferredWidth() const noexcept
{
    if (buttons.empty())
        return 0;

    const auto count = (int) buttons.size();

    return count * tokens::size::iconButton + (count - 1) * tokens::space::xxs;
}

void ToolStrip::updateButtons()
{
    for (size_t i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (entries[i].tool == tool, juce::dontSendNotification);
}

} // namespace dew
