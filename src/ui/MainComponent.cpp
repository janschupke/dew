#include "MainComponent.h"

#include "../BuildInfo.h"

namespace dew
{

MainComponent::MainComponent()
{
    titleLabel.setText ("dew", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::FontOptions (48.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    buildLabel.setText (BuildInfo::summary(), juce::dontSendNotification);
    buildLabel.setJustificationType (juce::Justification::centred);
    buildLabel.setFont (juce::FontOptions (13.0f));
    buildLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (buildLabel);

    setSize (1100, 700);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d21));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    titleLabel.setBounds (area.removeFromTop (area.getHeight() / 2).removeFromBottom (60));
    buildLabel.setBounds (area.removeFromTop (24));
}

} // namespace dew
