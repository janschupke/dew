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

// --- the panel every dialog's content is --------------------------------------

juce::Rectangle<int> Panel::contentBounds() const
{
    return embedded ? getLocalBounds() : getLocalBounds().reduced (tokens::space::xl);
}

int Panel::chromeHeight() const
{
    return embedded ? 0 : tokens::space::xl * 2;
}

void Panel::setEmbedded (bool shouldBeEmbedded)
{
    if (embedded == shouldBeEmbedded)
        return;

    embedded = shouldBeEmbedded;

    // Both, and in this order: the inset every layout is measured from has just
    // moved, and a panel that only repainted would draw its labels where its
    // controls are no longer.
    resized();
    repaint();
}

void Panel::paintBackground (juce::Graphics& g) const
{
    // An embedded panel is transparent, so the pane it sits in stays one
    // surface. Filling here would draw the background colour over whatever the
    // window had already painted - which is the same colour today and is the
    // window's decision rather than this panel's.
    if (! embedded)
        g.fillAll (tokens::colour::background);
}

juce::Rectangle<int> Panel::layOutFooter (juce::Rectangle<int>& area,
                                          std::initializer_list<juce::Button*> rightToLeft)
{
    const auto strip = area.removeFromBottom (tokens::size::controlHeight);
    auto remaining = strip;

    for (auto* button : rightToLeft)
    {
        if (button == nullptr)
            continue;

        button->setBounds (remaining.removeFromRight (buttonWidthFor (*button)));
        remaining.removeFromRight (tokens::space::md);
    }

    return strip;
}

void Panel::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

int buttonWidthFor (const juce::Button& button)
{
    using namespace tokens;

    // space::md each side is the inset DewButton draws its own label inside, and
    // space::xs beyond it so a word never touches the rounding.
    const auto word = juce::GlyphArrangement::getStringWidthInt (type::font (type::body),
                                                                 button.getButtonText());

    return juce::jmax (size::buttonMinWidth, word + (space::md + space::xs) * 2);
}

} // namespace dew::dialog
