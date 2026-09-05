#include "ui/AboutPanel.h"

#include "i18n/Strings.h"
#include "model/BuildInfo.h"
#include "ui/DewDialog.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{

/** Where the source is, which AGPLv3 obliges this window to point at.

    Not a catalogue key: a URL is an address rather than a sentence, and the one
    thing a translator must not do to it is translate it. README.md spells the
    same one, and that is a second copy of an address rather than of prose.
*/
const char* const sourceUrl = "https://github.com/janschupke/dew";

} // namespace

AboutPanel::AboutPanel()
{
    setComponentID ("about");

    setTitle (tr (StringId::about_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    // The title is said as well as the text, because DewButton is the one
    // primitive with no setTooltip override: juce::Button seeds its name from
    // the CONSTRUCTOR argument, once, and a button built with an empty label and
    // told its text afterwards is exactly the defect AccessibilityTests was
    // written for. What a screen reader reads is the sentence, not the word.
    sourceButton.setButtonText (tr (StringId::about_source));
    sourceButton.setTooltip (tr (StringId::about_sourceHelp));
    sourceButton.setTitle (tr (StringId::about_sourceHelp));
    sourceButton.onClick = [] { juce::URL (sourceUrl).launchInDefaultBrowser(); };
    addAndMakeVisible (sourceButton);

    closeButton.setButtonText (tr (StringId::about_close));
    closeButton.setTooltip (tr (StringId::about_close));
    closeButton.setTitle (tr (StringId::about_close));
    closeButton.onClick = [this] { closeDialog(); };
    addAndMakeVisible (closeButton);
}

juce::String AboutPanel::getBuildText() const
{
    return BuildInfo::summary();
}

void AboutPanel::closeDialog()
{
    // Null when the panel is built bare, which is how a test and dew_shot drive
    // it: there is no dialog to leave, and the buttons still do their work.
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState (0);
}

void AboutPanel::show (juce::Component* parent)
{
    dialog::launch (new AboutPanel(), tr (StringId::about_title), parent);
}

void AboutPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    auto area = getLocalBounds().reduced (space::xl);

    g.setColour (colour::textPrimary);
    g.setFont (type::font (type::display, true));
    g.drawFittedText (BuildInfo::name(), area.removeFromTop (size::stripHeading),
                      juce::Justification::centredLeft, 1);

    g.setFont (type::font (type::caption));
    g.setColour (colour::textSecondary);
    g.drawFittedText (tr (StringId::about_tagline), area.removeFromTop (size::controlHeightSm),
                      juce::Justification::centredLeft, 1);

    area.removeFromTop (space::lg);

    // Monospaced, because it is an identifier rather than prose: a commit hash
    // is read character by character and compared against another one.
    g.setColour (colour::textPrimary);
    g.setFont (type::monospaced (type::caption));
    g.drawFittedText (getBuildText(), area.removeFromTop (size::controlHeightSm),
                      juce::Justification::centredLeft, 1);

    area.removeFromTop (space::lg);

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawFittedText (tr (StringId::about_licence), area.removeFromTop (size::stripTransport),
                      juce::Justification::topLeft, 3);

    area.removeFromTop (space::md);

    g.drawFittedText (tr (StringId::about_thirdParty), area.removeFromTop (size::stripTransport),
                      juce::Justification::topLeft, 3);
}

void AboutPanel::resized()
{
    auto footer = getLocalBounds().reduced (space::xl).removeFromBottom (size::controlHeight);

    closeButton.setBounds (footer.removeFromRight (size::gutterLabel));
    footer.removeFromRight (space::md);
    sourceButton.setBounds (footer.removeFromRight (size::gutterLabel));
}

} // namespace dew
