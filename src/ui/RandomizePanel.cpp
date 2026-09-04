#include "ui/RandomizePanel.h"
#include "ui/DewDialog.h"

#include "ui/design/Tokens.h"

namespace dew
{

RandomizePanel::RandomizePanel (NoteTools::RandomizeOptions initial, juce::String scope)
    : scopeText (std::move (scope))
{
    setComponentID ("randomizePanel");

    velocityField.setRange (0.0, 1.0, 0.01);
    velocityField.setNumDecimalPlaces (2);
    velocityField.setCaption ("VELOCITY");
    velocityField.setTooltip (
        "How far a velocity can move, up or down. Zero leaves velocity alone");
    addAndMakeVisible (velocityField);

    stepField.setRange (0.0, 8.0, 1.0);
    stepField.setNumDecimalPlaces (0);
    stepField.setCaption ("TIMING");
    stepField.setSuffix (" steps");
    stepField.setTooltip ("How far a note can move in time, either way. Zero leaves timing alone");
    addAndMakeVisible (stepField);

    setOptions (initial);

    cancelButton.onClick = [this] { closeDialog(); };
    addAndMakeVisible (cancelButton);

    applyButton.onClick = [this]
    {
        // Read before closing: closing deletes this component, and the options
        // have to be off it by then.
        const auto options = getOptions();

        if (onApply)
            onApply (options);

        closeDialog();
    };
    addAndMakeVisible (applyButton);

    setSize (preferredWidth, preferredHeight);
}

NoteTools::RandomizeOptions RandomizePanel::getOptions() const
{
    return { velocityField.getValue(), (int) stepField.getValue() };
}

void RandomizePanel::setOptions (const NoteTools::RandomizeOptions& options)
{
    velocityField.setValue (options.velocityAmount, juce::dontSendNotification);
    stepField.setValue ((double) options.stepAmount, juce::dontSendNotification);
}

void RandomizePanel::closeDialog()
{
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState (0);
}

void RandomizePanel::show (NoteTools::RandomizeOptions initial, juce::String scopeText,
                           juce::Component* parent,
                           std::function<void (const NoteTools::RandomizeOptions&)> onApply)
{
    auto* panel = new RandomizePanel (initial, std::move (scopeText));
    panel->onApply = std::move (onApply);

    dialog::launch (panel, "Randomize", parent);
}

void RandomizePanel::paint (juce::Graphics& g)
{
    using namespace tokens;

    g.fillAll (colour::background);

    auto area = getLocalBounds().reduced (space::xl);

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::small));
    g.drawText (scopeText, area.removeFromTop (size::controlHeight),
                juce::Justification::centredLeft, true);

    g.setColour (colour::textDisabled);
    g.setFont (type::font (type::caption));
    g.drawText ("Zero leaves a property untouched.",
                getLocalBounds()
                    .reduced (space::xl)
                    .withTop (getHeight() - space::xl - size::controlHeight * 2 - space::sm)
                    .withHeight (size::controlHeight),
                juce::Justification::centredLeft, true);
}

void RandomizePanel::resized()
{
    using namespace tokens;

    auto area = getLocalBounds().reduced (space::xl);

    area.removeFromTop (size::controlHeight); // the scope sentence, painted
    area.removeFromTop (space::md);

    auto fields = area.removeFromTop (size::controlHeight + (int) type::caption + space::xs);
    const auto fieldWidth = (fields.getWidth() - space::md) / 2;

    velocityField.setBounds (fields.removeFromLeft (fieldWidth));
    fields.removeFromLeft (space::md);
    stepField.setBounds (fields.removeFromLeft (fieldWidth));

    auto buttons = getLocalBounds().reduced (space::xl).removeFromBottom (size::controlHeight);
    applyButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (space::sm);
    cancelButton.setBounds (buttons.removeFromRight (76));
}

} // namespace dew
