#include "ui/RandomizePanel.h"
#include "i18n/Strings.h"
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
    velocityField.setTooltip (tr (StringId::randomize_velocity_help));
    addAndMakeVisible (velocityField);

    stepField.setRange (0.0, 8.0, 1.0);
    stepField.setNumDecimalPlaces (0);
    stepField.setCaption ("TIMING");
    stepField.setSuffix (tr (StringId::unit_steps));
    stepField.setTooltip (tr (StringId::randomize_step_help));
    addAndMakeVisible (stepField);

    setOptions (initial);

    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);

    applyButton.onClick = [this]
    {
        // Read before closing: closing deletes this component, and the options
        // have to be off it by then.
        const auto options = getOptions();

        if (onApply)
            onApply (options);

        close();
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

void RandomizePanel::show (NoteTools::RandomizeOptions initial, juce::String scopeText,
                           juce::Component* parent,
                           std::function<void (const NoteTools::RandomizeOptions&)> onApply)
{
    auto* panel = new RandomizePanel (initial, std::move (scopeText));
    panel->onApply = std::move (onApply);

    dialog::launch (panel, tr (StringId::randomize_title), parent);
}

void RandomizePanel::paint (juce::Graphics& g)
{
    using namespace tokens;

    paintBackground (g);

    auto area = contentBounds();

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::small));
    g.drawText (scopeText, area.removeFromTop (size::controlHeight),
                juce::Justification::centredLeft, true);

    // Directly above the footer, measured from the bottom of the content rather
    // than from the panel's own height - which is the same rectangle right up
    // until the inset stops being applied.
    const auto content = contentBounds();

    g.setColour (colour::textDisabled);
    g.setFont (type::font (type::caption));
    g.drawText (tr (StringId::randomize_hint),
                content.withTop (content.getBottom() - size::controlHeight * 2 - space::sm)
                    .withHeight (size::controlHeight),
                juce::Justification::centredLeft, true);
}

void RandomizePanel::resized()
{
    using namespace tokens;

    auto area = contentBounds();

    area.removeFromTop (size::controlHeight); // the scope sentence, painted
    area.removeFromTop (space::md);

    // Asked, not derived. This was controlHeight + (int) type::caption +
    // space::xs, which casts a font size to a pixel count and lands on 41 -
    // beside an effect card's 40 and a settings row's 26, for the same control.
    auto fields = area.removeFromTop (velocityField.preferredHeight());
    const auto fieldWidth = (fields.getWidth() - space::md) / 2;

    velocityField.setBounds (fields.removeFromLeft (fieldWidth));
    fields.removeFromLeft (space::md);
    stepField.setBounds (fields.removeFromLeft (fieldWidth));

    auto footer = contentBounds();
    layOutFooter (footer, { &applyButton, &cancelButton });
}

} // namespace dew
