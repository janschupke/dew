#include "ui/DewDialog.h"

#include "ui/design/Tokens.h"

namespace dew::dialog
{

void launch (juce::Component* content, const juce::String& title, juce::Component* centreAround)
{
    juce::DialogWindow::LaunchOptions options;

    options.content.setOwned (content);
    options.dialogTitle = title;
    options.dialogBackgroundColour = tokens::colour::background;
    options.componentToCentreAround = centreAround;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

} // namespace dew::dialog
