#include "ui/DewDialog.h"

#include <limits>

#include "ui/design/Tokens.h"

namespace dew::dialog
{

namespace
{

/** A dialog's content, scrolled, for the case where the screen is too short.

    Owns the panel it was given, so `content.setOwned` still owns exactly one
    thing and the ownership contract in the header is unchanged.
*/
class ScrollingContent final : public juce::Component
{
public:
    ScrollingContent (juce::Component* content, int height)
    {
        viewport.setViewedComponent (content, true);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        // Wider by the scrollbar, so the panel inside keeps the width it was
        // laid out for rather than losing ten pixels off its right-hand column.
        setSize (content->getWidth() + viewport.getScrollBarThickness(), height);
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
    }

private:
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrollingContent)
};

} // namespace

int maxContentHeight()
{
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();

    if (display == nullptr)
        return std::numeric_limits<int>::max();

    // userBounds already excludes the menu bar and the dock. What it does
    // not exclude is the dialog's own title bar and a margin for the window not
    // being centred perfectly, which is what tokens::space::xxl stands in for.
    return juce::jmax (tokens::size::stripTransport * 4,
                       juce::roundToInt (display->userBounds.getHeight()) - tokens::space::xxl * 2);
}

void launch (juce::Component* content, const juce::String& title, juce::Component* centreAround)
{
    juce::DialogWindow::LaunchOptions options;

    if (const auto tallest = maxContentHeight(); content->getHeight() > tallest)
        options.content.setOwned (new ScrollingContent (content, tallest));
    else
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
