#include "ui/ConfirmPanel.h"

#include "ui/DewDialog.h"
#include "ui/design/Tokens.h"

namespace dew
{

ConfirmPanel::ConfirmPanel (Request r)
    : request (std::move (r))
{
    setComponentID ("confirmPanel");

    confirmButton.setButtonText (request.confirmText);

    cancelButton.onClick = [this] { closeDialog(); };
    addAndMakeVisible (cancelButton);

    confirmButton.onClick = [this]
    {
        // Read before closing, the way RandomizePanel does: closing deletes
        // this component, so nothing may be touched afterwards - including the
        // callback, which is a member.
        auto confirmed = onConfirm;

        closeDialog();

        if (confirmed)
            confirmed();
    };
    addAndMakeVisible (confirmButton);

    setSize (preferredWidth, preferredHeight);
}

void ConfirmPanel::closeDialog()
{
    // Null when the panel is built bare, which is how a test drives it: there
    // is no dialog to leave, and the buttons still do their work.
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState (0);
}

void ConfirmPanel::show (Request r, juce::Component* parent, std::function<void()> onConfirmed)
{
    const auto title = r.title;

    auto* panel = new ConfirmPanel (std::move (r));
    panel->onConfirm = std::move (onConfirmed);

    dialog::launch (panel, title, parent);
}

void ConfirmPanel::paint (juce::Graphics& g)
{
    using namespace tokens;

    g.fillAll (colour::background);

    auto area = getLocalBounds().reduced (space::xl);
    area.removeFromBottom (size::controlHeight + space::xl);

    g.setColour (colour::textPrimary);
    g.setFont (type::font (type::body));

    // Wrapped rather than elided: the sentence names the thing being deleted
    // and says what goes with it, and a name cut off halfway is the one word
    // the reader needed.
    g.drawFittedText (request.message, area, juce::Justification::topLeft, 3);
}

void ConfirmPanel::resized()
{
    using namespace tokens;

    auto area = getLocalBounds().reduced (space::xl);
    auto buttons = area.removeFromBottom (size::controlHeight);

    // Right-aligned, acting button outermost, which is where the other dew
    // dialogs put theirs.
    confirmButton.setBounds (buttons.removeFromRight (96));
    buttons.removeFromRight (space::md);
    cancelButton.setBounds (buttons.removeFromRight (96));
}

ConfirmHook confirmWithPanel (juce::Component* parent)
{
    return [parent] (ConfirmPanel::Request request, std::function<void()> confirmed)
    { ConfirmPanel::show (std::move (request), parent, std::move (confirmed)); };
}

} // namespace dew
